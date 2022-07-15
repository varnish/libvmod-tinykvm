#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "gettid.hpp"
#include "varnish.hpp"
#include <cstring>
#include <tinykvm/rsp_client.hpp>

namespace kvm {
static constexpr uint64_t EXTRA_CPU_STACK_SIZE = 0x100000;
static constexpr int EXTRA_CPU_ID = 16;

VMPoolItem::VMPoolItem(const MachineInstance& main_vm,
	const vrt_ctx* ctx, TenantInstance* ten, ProgramInstance* prog)
{
	// Spawn forked VM on dedicated thread, blocking.
	tp.enqueue(
	[&] () -> long {
		try {
			this->mi = std::make_unique<MachineInstance> (
				main_vm, ctx, ten, prog);
			return 0;
		} catch (...) {
			return -1;
		}
	}).get();
}

ProgramInstance::ProgramInstance(
	std::vector<uint8_t> elf,
	const vrt_ctx* ctx, TenantInstance* ten,
	bool debug)
	: binary{std::move(elf)},
	  m_main_queue {1},
	  m_main_async_queue {1},
	  entry_address {},
	  rspclient{nullptr}
{
	this->m_future = m_main_queue.enqueue(
	[=] () -> long {
		try {
			main_vm = std::make_unique<MachineInstance>
				(binary, ctx, ten, this, debug);

			main_vm_extra_cpu_stack = EXTRA_CPU_STACK_SIZE +
				main_vm->machine().mmap_allocate(EXTRA_CPU_STACK_SIZE);

			main_vm->initialize();

			const size_t max_vms = ten->config.group.max_concurrency;
			for (size_t i = 0; i < max_vms; i++) {
				// Instantiate forked VMs on dedicated threads,
				// in order to prevent KVM migrations.
				m_vms.emplace_back(*main_vm, ctx, ten, this);
				m_vmqueue.enqueue(&m_vms.back());
			}
		} catch (...) {
			return -1;
		}

		return 0;
	});
}
ProgramInstance::~ProgramInstance()
{
}

long ProgramInstance::wait_for_initialization()
{
	auto code = this->m_future.get();

	if (main_vm == nullptr) {
		throw std::runtime_error("The program failed to initialize. Check logs for crash?");
	}

	if (!main_vm->is_waiting_for_requests()) {
		throw std::runtime_error("The main program was not waiting for requests. Did you forget to call 'wait_for_requests()'?");
	}

	return code;
}

uint64_t ProgramInstance::lookup(const char* name) const
{
	return main_vm->resolve_address(name);
}
ProgramInstance::gaddr_t ProgramInstance::entry_at(const int idx) const
{
	return entry_address.at(idx);
}
void ProgramInstance::set_entry_at(const int idx, gaddr_t addr)
{
	entry_address.at(idx) = addr;
}

Reservation ProgramInstance::reserve_vm(const vrt_ctx* ctx,
	TenantInstance*, std::shared_ptr<ProgramInstance>& prog)
{
	VMPoolItem* slot = nullptr;
	m_vmqueue.wait_dequeue(slot);
	assert(slot && ctx);

	/* Reset and reference the active program. */
	slot->mi->reset_to(ctx, *prog->main_vm);

	/* This creates a self-reference, which ensures that open
	   Machine instances will keep the program instance alive. */
	slot->prog_ref = prog;

	/* What happens when the transaction is done */
	return {slot, [] (void* slotv) {
		auto* slot = (VMPoolItem *)slotv;
		auto* mi = slot->mi.get();
		mi->tail_reset();
		// Signal waiters that slot is ready again
		// If there any waiters, they keep the program referenced
		mi->instance().m_vmqueue.enqueue(slot);
		// Last action: Unassign program, which can destruct the program
		slot->prog_ref = nullptr;
	}};
}

long ProgramInstance::storage_call(tinykvm::Machine& src, gaddr_t func,
	size_t n, VirtBuffer buffers[], gaddr_t res_addr, size_t res_size)
{
	/* Detect wrap-around */
	if (UNLIKELY(res_addr + res_size < res_addr))
		return -1;

	auto future = m_main_queue.enqueue(
	[&] () -> long
	{
		auto& stm = main_vm->machine();
		uint64_t vaddr = stm.stack_address();

		for (size_t i = 0; i < n; i++) {
			vaddr -= buffers[i].len;
			vaddr &= ~(uint64_t)0x7;
			stm.copy_from_machine(vaddr, src, buffers[i].addr, buffers[i].len);
			buffers[i].addr = vaddr;
		}

		vaddr -= n * sizeof(VirtBuffer);
		const uint64_t stm_bufaddr = vaddr;
		stm.copy_to_guest(stm_bufaddr, buffers, n * sizeof(VirtBuffer));

		const uint64_t new_stack = vaddr;

		/* We need to use the CTX from the current program */
		auto& inst = *src.get_userdata<MachineInstance>();
		main_vm->set_ctx(inst.ctx());
		assert(main_vm->ctx());

		try {
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			stm.set_registers(regs);
			stm.run(3.0);
			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			if (res_addr != 0x0 && st_res_buffer != 0x0) {
				/* Copy from the storage machine back into tenant VM instance */
				src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
			}
			/* Run the function to the end, allowing cleanup */
			stm.run(3.0);
			/* If res_addr is zero, we will just return the
			   length provided by storage as-is, to allow some
			   communication without a buffer. */
			return (res_addr != 0) ? st_res_size : regs.rsi;
		} catch (...) {
			return -1;
		}
	});
	return future.get();
}

long ProgramInstance::async_storage_call(bool async, gaddr_t func, gaddr_t arg)
{
	std::scoped_lock lock(m_async_mtx);
	// Avoid the last task in case it is still active
	while (m_async_tasks.size() > 1)
		// TODO: Read the return value of the tasks to detect errors
		m_async_tasks.pop_front();

	// Block and finish previous async tasks
	if (async == false)
	{
		m_async_tasks.push_back(
			m_main_queue.enqueue(
		[=] () -> long
		{
			auto& stm = main_vm->machine();
			const uint64_t new_stack = stm.stack_address();

			/* This vmcall has no attached VRT_CTX. */
			main_vm->set_ctx(nullptr);

			try {
				tinykvm::tinykvm_x86regs regs;
				stm.setup_call(regs, func, new_stack,
					(uint64_t)arg);
				stm.set_registers(regs);
				stm.run(3.0);
				return 0;
			} catch (...) {
				return -1;
			}
		}));
	} else {
		/* Use separate queue: m_main_async_queue. */
		m_async_tasks.push_back(
			m_main_async_queue.enqueue(
		[=] () -> long
		{
			/* This vmcall has no attached VRT_CTX. */
			main_vm->set_ctx(nullptr);

			try {
				if (!main_vm_extra_cpu) {
					main_vm_extra_cpu.reset(new tinykvm::vCPU);
					main_vm_extra_cpu->smp_init(EXTRA_CPU_ID, main_vm->machine());
				}
				tinykvm::tinykvm_x86regs regs;
				main_vm->machine().setup_call(regs, func,
					main_vm_extra_cpu_stack, (uint64_t)arg);
				main_vm_extra_cpu->set_registers(regs);
				//printf("Working from vCPU %d, RIP=0x%llX  FUN=0x%llX  RSP=0x%llX  ARG=0x%llX\n",
				//	   main_vm_extra_cpu->cpu_id, regs.rip, regs.r15, regs.rsp, regs.rsi);

				main_vm_extra_cpu->run(3.0);
				return 0;
			}
			catch (const tinykvm::MachineException& me) {
				printf("Async storage task error: %s (0x%lX)\n",
					me.what(), me.data());
				return -1;
			} catch (const std::exception& e) {
				printf("Async storage task error: %s\n", e.what());
				return -1;
			}
		}));
	}
	return 0;
}

long ProgramInstance::live_update_call(const vrt_ctx* ctx,
	gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc)
{
	struct SerializeResult {
		uint64_t data;
		uint64_t len;
	};

	auto future = m_main_queue.enqueue(
	[&] () -> SerializeResult
	{
		try {
			/* Serialize data in the old machine */
			main_vm->set_ctx(ctx);
			auto& old_machine = main_vm->machine();
			old_machine.timed_vmcall(func,
				main_vm->tenant().config.max_time());
			/* Get serialized data */
			auto regs = old_machine.registers();
			auto data_addr = regs.rdi;
			auto data_len  = regs.rsi;
			if (data_addr + data_len < data_addr) {
				return {0, 0};
			}
			return {data_addr, data_len};
		} catch (...) {
			/* We have to make sure Varnish is not taken down */
			return {0x0, 0};
		}
	});

	SerializeResult from = future.get();
	if (from.data == 0x0)
		return -1;

	auto new_future = new_prog.m_main_queue.enqueue(
	[&] () -> long
	{
		try {
			auto &new_machine = new_prog.main_vm->machine();
			/* Begin resume procedure */
			new_prog.main_vm->set_ctx(ctx);

			new_machine.timed_vmcall(newfunc,
				new_prog.main_vm->tenant().config.max_time(),
				(uint64_t)from.len);

			auto regs = new_machine.registers();
			/* The machine should be calling STOP with rsi=dst_data */
			auto res_data = regs.rdi;
			auto res_size = std::min((uint64_t)regs.rsi, from.len);
			if (res_data != 0x0)
			{ // Just a courtesy, we *do* check permissions.
				auto& old_machine = main_vm->machine();
				new_machine.copy_from_machine(
					res_data, old_machine, from.data, res_size);
				/* Resume the new machine, allowing it to deserialize data */
				new_machine.run(2.0);
				return res_size;
			}
			return 0;
		}
		catch (...)
		{
			return -1;
		}
	});

	return new_future.get();
}

} // kvm
