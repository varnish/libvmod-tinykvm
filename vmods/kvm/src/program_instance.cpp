#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "gettid.hpp"
#include "varnish.hpp"
#include <cstring>
#include <tinykvm/rsp_client.hpp>

namespace kvm {

VMPoolItem::VMPoolItem(const MachineInstance& main_vm,
	const vrt_ctx* ctx, TenantInstance* ten, ProgramInstance* prog)
{
	tp.enqueue(
	[&] () -> long
	{
		this->mi = std::make_unique<MachineInstance> (
			main_vm, ctx, ten, prog);
		return 0;
	}).get();
}

ProgramInstance::ProgramInstance(
	std::vector<uint8_t> elf,
	const vrt_ctx* ctx, TenantInstance* ten,
	bool debug)
	: binary{std::move(elf)},
	  main_vm{binary, ctx, ten, this, debug},
	  m_main_queue {1},
	  rspclient{nullptr}
{
	if (!main_vm.is_waiting_for_requests()) {
		throw std::runtime_error("The main program was not waiting for requests. Did you forget to call 'wait_for_requests()'?");
	}

	const size_t max_vms = ten->config.group.max_concurrency;
	for (size_t i = 0; i < max_vms; i++) {
		// Instantiate forked VMs on dedicated threads,
		// in order to prevent KVM migrations.
		m_vms.emplace_back(main_vm, ctx, ten, this);
		m_vmqueue.enqueue(&m_vms.back());
	}
}
ProgramInstance::~ProgramInstance()
{
}

uint64_t ProgramInstance::lookup(const char* name) const
{
	return main_vm.resolve_address(name);
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
	slot->mi->reset_to(ctx, prog->main_vm);

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
		auto& stm = main_vm.machine();
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
		main_vm.set_ctx(inst.ctx());
		assert(main_vm.ctx());

		try {
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			constexpr uint32_t ONE_SECOND = 62'500'000;
			const uint32_t ticks = ONE_SECOND
#else
			const uint32_t ticks = 0;
#endif
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, ticks, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			stm.set_registers(regs);
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			stm.run();
#else
			stm.run(1.0);
#endif
			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			if (res_addr != 0x0 && st_res_buffer != 0x0) {
				/* Copy from the storage machine back into tenant VM instance */
				src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
			}
			/* Run the function to the end, allowing cleanup */
			stm.run(1.0);
			return st_res_size;
		} catch (...) {
			return -1;
		}
	});
	return future.get();
}

long ProgramInstance::async_storage_call(gaddr_t func, gaddr_t arg)
{
	// Block and finish previous async tasks
	// TODO: Read the return value of the tasks to detect errors
	m_async_tasks.clear();

	m_async_tasks.push_back(
		m_main_queue.enqueue(
	[=] () -> long
	{
		auto& stm = main_vm.machine();
		const uint64_t new_stack = main_vm.machine().stack_address();

		/* This vmcall has no attached VRT_CTX. */
		main_vm.set_ctx(nullptr);

		try {
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			constexpr uint32_t ONE_SECOND = 62'500'000;
			const uint32_t ticks = ONE_SECOND
#else
			const uint32_t ticks = 0;
#endif
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, ticks, new_stack,
				(uint64_t)arg);
			stm.set_registers(regs);
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			stm.run();
#else
			stm.run(1.0);
#endif
			return 0;
		} catch (...) {
			return -1;
		}
	}));
	return 0;
}

long ProgramInstance::live_update_call(const vrt_ctx* ctx,
	gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc)
{
	struct SerializeResult {
		std::unique_ptr<char[]>  data;
		unsigned long long len;
	};

	auto future = m_main_queue.enqueue(
	[&] () -> SerializeResult
	{
		try {
			/* Serialize data in the old machine */
			main_vm.set_ctx(ctx);
			auto& old_machine = main_vm.machine();
			old_machine.timed_vmcall(func,
				main_vm.tenant().config.max_time());
			/* Get serialized data */
			auto regs = old_machine.registers();
			auto data_addr = regs.rdi;
			auto data_len  = regs.rsi;
			if (data_addr + data_len < data_addr) {
				return {nullptr, 0};
			}
			char* data = new char[data_len];
			old_machine.copy_from_guest(data, data_addr, data_len);
			return {std::unique_ptr<char[]> (data), data_len};
		} catch (...) {
			/* We have to make sure Varnish is not taken down */
			return {nullptr, 0};
		}
	});

	SerializeResult res = future.get();
	if (res.data == nullptr)
		return -1;

	auto& new_machine = new_prog.main_vm.machine();
	/* Begin resume procedure */
	new_prog.main_vm.set_ctx(ctx);
	new_machine.timed_vmcall(newfunc,
		new_prog.main_vm.tenant().config.max_time(),
		(uint64_t)res.len);
	auto new_regs = new_machine.registers();
	/* The machine should be calling STOP with rsi=dst_data */
	auto res_data = new_regs.rdi;
	auto res_size = std::min(new_regs.rsi, res.len);
	if (res_data != 0x0) { // Just a courtesy, we *do* check permissions.
		new_machine.copy_to_guest(
			res_data, res.data.get(), res_size);
		/* Resume the new machine, allowing it to deserialize data */
		new_machine.run();
	}
	return 0;
}

} // kvm
