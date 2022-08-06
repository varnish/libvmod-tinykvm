/**
 * @file program_instance.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Wrapper for a live instance of a KVM program.
 * @version 0.1
 * @date 2022-07-23
 *
 * A program instance wraps all the features of a single program
 * into one structure that can be hot-swapped at run-time. The
 * features include the program, the thread pool used to communicate
 * with the program. The binary and main VM (used as storage), the
 * TP for communicating with main VM. A timer system for handling
 * async tasks (into storage), and stuff needed to live-debug the
 * program.
 * 
 * There is also a separate vCPU (with TP) for fully asynchronous
 * tasks that can use storage at the same time as requests. It is
 * recommended to use the asynchronous vCPU only for safe things
 * like fetching from Varnish in order to produce content.
 * 
**/
#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "settings.hpp"
#include "varnish.hpp"
#include <cstring>
#include <tinykvm/rsp_client.hpp>
extern "C" int usleep(uint32_t usec);

namespace kvm {
extern void libadns_untag(const std::string&, struct vcl*);
static constexpr bool VERBOSE_STORAGE_TASK = false;

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
	  m_main_queue {1, STORAGE_VM_NICE, false},
	  m_main_async_queue {1, ASYNC_STORAGE_NICE, ASYNC_STORAGE_LOWPRIO},
	  entry_address {},
	  m_vcl {ctx->vcl},
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
			/* Make sure we signal that there is no program, if the
			   program fails to intialize. */
			main_vm = nullptr;
			return -1;
		}

		/* We do not have a storage CTX after this point. */
		main_vm->set_ctx(nullptr);
		this->initialization_complete = true;
		return 0;
	});
}
ProgramInstance::~ProgramInstance()
{
	if (main_vm_extra_cpu) {
		main_vm_extra_cpu->deinit();
	}
	for (const auto& adns : m_adns_tags) {
		if (!adns.tag.empty())
			libadns_untag(adns.tag, this->m_vcl);
	}
}

long ProgramInstance::wait_for_initialization()
{
	if (!this->m_future.valid()) {
		//fprintf(stderr, "Skipped over %s\n", main_vm->tenant().config.name.c_str());
		return 0;
	}
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
	TenantInstance*, std::shared_ptr<ProgramInstance> prog)
{
	VMPoolItem* slot = nullptr;
	m_vmqueue.wait_dequeue(slot);
	assert(slot && ctx);

	/* Reset and reference the active program. */
	slot->mi->reset_to(ctx, *prog->main_vm);

	/* This creates a self-reference, which ensures that open
	   Machine instances will keep the program instance alive. */
	slot->prog_ref = std::move(prog);

	/* What happens when the transaction is done */
	return {slot, [] (void* slotv) {
		auto* slot = (VMPoolItem *)slotv;
		auto& mi = *slot->mi;
		mi.tail_reset();
		// Signal waiters that slot is ready again
		// If there any waiters, they keep the program referenced
		mi.program().m_vmqueue.enqueue(slot);
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

	if constexpr (VERBOSE_STORAGE_TASK) {
		printf("Storage task on main queue\n");
	}
	auto future = m_main_queue.enqueue(
	[&] () -> long
	{
		if constexpr (VERBOSE_STORAGE_TASK) {
			printf("-> Storage task on main queue ENTERED\n");
		}
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

		const uint64_t new_stack = vaddr & ~0xFL;

		try {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("Storage task calling 0x%lX with stack 0x%lX\n",
					func, new_stack);
			}
			const float timeout = main_vm->tenant().config.max_storage_time();
			main_vm->begin_call();

			/* Build call manually. */
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			regs.rip = stm.reentry_address();
			stm.set_registers(regs);

			/* Check if this is a debug program. */
			if (main_vm->is_debug()) {
				main_vm->storage_debugger(timeout);
			} else {
				stm.run(timeout);
			}

			//stm.timed_reentry_stack(func, new_stack, timeout,
			//	(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);

			/* The machine must be stopped, and it must have called storage_return. */
			if (!stm.stopped() || !main_vm->response_called(2)) {
				throw std::runtime_error("Storage did not respond properly");
			}

			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			if (res_addr != 0x0 && st_res_buffer != 0x0) {
				/* Copy from the storage machine back into tenant VM instance */
				src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
			}
			/* Resume, run the function to the end, allowing cleanup */
			stm.run(STORAGE_CLEANUP_TIMEOUT);
			/* If res_addr is zero, we will just return the
			   length provided by storage as-is, to allow some
			   communication without a buffer. */
			const auto retval = (res_addr != 0) ? st_res_size : regs.rsi;

			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Storage task on main queue returning %llu to 0x%lX\n",
					retval, st_res_buffer);
			}
			return retval;

		} catch (const std::exception& e) {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Storage task on main queue failed: %s\n",
					e.what());
			}
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
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("-> Async task on main queue\n");
			}
			auto& stm = main_vm->machine();

			try {
				if constexpr (VERBOSE_STORAGE_TASK) {
					printf("Calling 0x%lX\n", func);
				}
				/* Avoid async storage while still initializing. 10ms intervals. */
				if (!this->initialization_complete) {
					usleep(10'000);
				}
				stm.timed_reentry(func, ASYNC_STORAGE_TIMEOUT,
					(uint64_t)arg);
				if constexpr (VERBOSE_STORAGE_TASK) {
					printf("<- Async task finished 0x%lX\n", func);
				}
				return 0;
			} catch (const std::exception& e) {
				if constexpr (VERBOSE_STORAGE_TASK) {
					printf("<- Async task failure: %s\n", e.what());
				}
				return -1;
			}
		}));
	} else {
		/* Use separate queue: m_main_async_queue. */
		m_async_tasks.push_back(
			m_main_async_queue.enqueue(
		[=] () -> long
		{
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("-> Async task on async queue\n");
			}
			auto& stm = main_vm->machine();

			try {
				if constexpr (VERBOSE_STORAGE_TASK) {
					printf("Calling 0x%lX with stack 0x%lX\n",
						func, main_vm_extra_cpu_stack);
				}
				/* Avoid async storage while still initializing. 10ms intervals. */
				if (!this->initialization_complete) {
					usleep(10'000);
				}
				if (!main_vm_extra_cpu) {
					main_vm_extra_cpu.reset(new tinykvm::vCPU);
					main_vm_extra_cpu->smp_init(EXTRA_CPU_ID, stm);
				}
				/* For the love of GOD don't try to change this to
				   a timed_reentry_stack call *again*. This function
				   specfically uses *main_vm_extra_cpu*. */
				tinykvm::tinykvm_x86regs regs;
				stm.setup_call(regs, func, main_vm_extra_cpu_stack, (uint64_t)arg);
				//regs.rip = stm.reentry_address();
				main_vm_extra_cpu->set_registers(regs);
				main_vm_extra_cpu->run(ASYNC_STORAGE_TIMEOUT);
				if constexpr (VERBOSE_STORAGE_TASK) {
					printf("<- Async task finished 0x%lX\n", func);
				}
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
	const float timeout = main_vm->tenant().config.max_storage_time();

	auto future = m_main_queue.enqueue(
	[&] () -> SerializeResult
	{
		try {
			/* Serialize data in the old machine */
				main_vm->set_ctx(ctx);
				auto &old_machine = main_vm->machine();
				old_machine.timed_vmcall(func, timeout);
				/* Get serialized data */
				auto regs = old_machine.registers();
				auto data_addr = regs.rdi;
				auto data_len = regs.rsi;
				if (data_addr + data_len < data_addr)
				{
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

			new_machine.timed_vmcall(newfunc, timeout, (uint64_t)from.len);

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
				new_machine.run(STORAGE_DESERIALIZE_TIMEOUT);
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
