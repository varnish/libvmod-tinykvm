extern "C" {
	//long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir);
	void kvm_SetCacheable(VRT_CTX, bool c);
	void kvm_SetTTLs(VRT_CTX, float ttl, float grace, float keep);
}
#include <tinykvm/smp.hpp>

namespace kvm {

static void syscall_register_func(vCPU& cpu, MachineInstance& inst)
{
	// Register callback function for tenant
	const auto& regs = cpu.registers();
	if (inst.is_waiting_for_requests() == false)
	{
		const uint64_t KERNEL_END = cpu.machine().kernel_end_address();
		const uint64_t MEMORY_END = cpu.machine().max_address();
		if (UNLIKELY(regs.rsi < KERNEL_END || regs.rsi >= MEMORY_END)) {
			fprintf(stderr, "register_func: Function %lld is now 0x%llX\n",
					regs.rdi, regs.rsi);
			throw std::runtime_error("Invalid address for register_func provided");
		}
		inst.program().set_entry_at(regs.rdi, regs.rsi);
	} else {
		throw std::runtime_error("register_func(): Cannot be called after initialization");
	}
}
static void syscall_wait_for_requests(vCPU& cpu, MachineInstance& inst)
{
	if (inst.is_waiting_for_requests() == false) {
		// Wait for events (stop machine)
		inst.wait_for_requests();
		cpu.stop();
	} else {
		throw std::runtime_error("wait_for_requests(): Cannot be called after initialization");
	}
}

static void syscall_backend_response(vCPU& cpu, MachineInstance& inst)
{
	inst.finish_call(1);
	cpu.stop();
}
static void syscall_backend_streaming_response(vCPU& cpu, MachineInstance& inst)
{
	inst.finish_call(10);
	cpu.stop();
}
static void syscall_storage_return(vCPU& cpu, MachineInstance& inst)
{
	inst.finish_call(2);
	cpu.stop();
}
static void syscall_storage_noreturn(vCPU& cpu, MachineInstance& inst)
{
	inst.finish_call(3);
	cpu.stop();
}

static void syscall_set_cacheable(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	if (inst.ctx()->bo) {
		kvm_SetCacheable(inst.ctx(), regs.rdi);
		kvm_SetTTLs(inst.ctx(), // TTL, GRACE, KEEP
			regs.rsi / 1000.0, regs.rdx / 1000.0, regs.rcx / 1000.0);
		regs.rax = 0;
	} else {
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

static void syscall_shared_memory(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	regs.rax = inst.shared_memory_boundary();
	regs.rdx = inst.shared_memory_boundary() + inst.shared_memory_size();
	cpu.set_registers(regs);
}

static void syscall_make_ephemeral(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	if (inst.is_waiting_for_requests() == false) {
		if (inst.tenant().config.control_ephemeral()) {
			inst.set_ephemeral(regs.rdi != 0);
			regs.rax = 0;
		} else {
			const auto& grname = inst.tenant().config.group.name;
			// XXX: We *really* need a wrapper for this.
			if (inst.ctx() && inst.ctx()->vsl) {
				VSLb(inst.ctx()->vsl, SLT_VCL_Log,
					"%s: Cannot change ephemeralness. Option 'control_ephemeral' not enabled (group: %s)",
					inst.name().c_str(), grname.c_str());
			}
			fprintf(stderr,
				"%s: Cannot change ephemeralness. Option 'control_ephemeral' not enabled (group: %s)\n",
				inst.name().c_str(), grname.c_str());
			regs.rax = -1;
		}
		cpu.set_registers(regs);
	} else {
		throw std::runtime_error("Cannot change ephemeralness after initialization");
	}
}

static void syscall_is_storage(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	regs.rax = inst.is_storage();
	cpu.set_registers(regs);
}
static void syscall_storage_allow(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	// Only from storage, and only during initialization
	if (inst.is_storage() && inst.is_waiting_for_requests() == false) {
		inst.program().storage().allow_list.insert(regs.rdi);
		regs.rax = 0;
	} else {
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

static void syscall_storage_callv(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	const size_t n = regs.rsi;
	if (!inst.is_storage() && n <= 64) {
		VirtBuffer buffers[64];
		cpu.machine().copy_from_guest(buffers, regs.rdx, n * sizeof(VirtBuffer));
		regs.rax = inst.program().storage_call(cpu.machine(),
		/*  func      buf vector    dst     dstsize */
			regs.rdi, n, buffers, regs.rcx, regs.r8);
	} else {
		/* Prevent deadlock waiting for storage, while in storage. */
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_storage_task(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	const uint64_t function = regs.rdi;
	const uint64_t argument = regs.rsi;
	const size_t   arglen   = regs.rdx;
	const uint64_t start    = regs.rcx;
	const uint64_t period   = regs.r8;

	const std::string task_buffer =
		cpu.machine().buffer_to_string(argument, arglen, STORAGE_TASK_MAX_ARGUMENT);

	if (start == 0 && period == 0) {
		regs.rax = inst.program().storage_task(
			function, std::move(task_buffer));
	} else {
		/* XXX: Racy spam avoidance of async tasks. */
		auto *prog = &inst.program();
		if (prog->m_timer_system.racy_count() < STORAGE_TASK_MAX_TIMERS) {
			regs.rax = prog->m_timer_system.add(
				std::chrono::milliseconds(start),
				[=, argument = std::move(task_buffer)](auto)
				{
					try {
						prog->storage_task(function, std::move(argument));
					} catch (const std::exception& e) {
						/* XXX: We have nowhere to post this error */
						//fprintf(stderr, "");
					}
				},
				std::chrono::milliseconds(period));
		} else {
			/* TODO: Log the reason behind the failure. */
			regs.rax = -1;
		}
	}
	cpu.set_registers(regs);
}
static void syscall_stop_storage_task(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	regs.rax = inst.program().m_timer_system.remove(regs.rdi);
	cpu.set_registers(regs);
}

static void syscall_multiprocess(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least 2 vCPUs");
		} else if (UNLIKELY(regs.rdi > inst.tenant().config.max_smp())) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		cpu.machine().smp().timed_smpcall(num_cpus,
			cpu.machine().mmap_allocate(num_cpus * stack_size),
			stack_size,
			(uint64_t) regs.rsi, /* func */
			8.0f, /* TODO: Tenant-property */
			(uint64_t) regs.rdx, /* arg1 */
			(uint64_t) regs.rcx, /* arg2 */
			(uint64_t) regs.r8,  /* arg3 */
			(uint64_t) regs.r9); /* arg4 */
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess exception: %s\n", e.what());
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_multiprocess_array(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > inst.tenant().config.max_smp())) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		cpu.machine().smp().timed_smpcall_array(num_cpus,
			cpu.machine().mmap_allocate(num_cpus * stack_size),
			stack_size,
			(uint64_t) regs.rsi,  /* func */
			8.0f, /* TODO: Tenant-property */
			(uint64_t) regs.rdx,  /* array */
			(uint32_t) regs.rcx); /* array_size */
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess exception: %s\n", e.what());
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_multiprocess_clone(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least 2 vCPUs");
		} else if (UNLIKELY(regs.rdi > inst.tenant().config.max_smp())) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		cpu.machine().smp().timed_smpcall_clone(num_cpus,
			regs.rsi,
			regs.rdx,
			8.0f, /* TODO: Tenant-property */
			regs);
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess exception: %s\n", e.what());
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_multiprocess_wait(vCPU& cpu, MachineInstance&)
{
	auto& regs = cpu.registers();
	try {
		/* XXX: Propagate SMP exceptions */
		cpu.machine().smp_wait(); // No-op when SMP never used
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess wait exception: %s\n", e.what());
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

static void syscall_memory_info(vCPU& cpu, MachineInstance &inst)
{
	const struct {
		uint64_t max_memory;
		uint64_t max_workmem;
		uint64_t workmem_upper;
		uint64_t workmem_current;
	} meminfo {
		.max_memory = inst.tenant().config.max_main_memory(),
		.max_workmem = inst.tenant().config.max_req_memory(),
		.workmem_upper = cpu.machine().banked_memory_capacity_bytes(),
		.workmem_current = cpu.machine().banked_memory_bytes(),
	};
	const auto& regs = cpu.registers();
	cpu.machine().copy_to_guest(regs.rdi, &meminfo, sizeof(meminfo));
}

static void syscall_is_debug(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	regs.rax = inst.is_debug();
	cpu.set_registers(regs);
}

static void syscall_breakpoint(vCPU& cpu, MachineInstance& inst)
{
	auto& regs = cpu.registers();
	if (inst.ctx() && inst.ctx()->vsl) {
		if (inst.is_debug() || inst.allows_debugging()) {
			VSLb(inst.ctx()->vsl, SLT_VCL_Log,
				"VM breakpoint at 0x%lX", (long) regs.rip);
			inst.open_debugger(DEBUG_PORT, inst.max_req_time());
		} else {
			VSLb(inst.ctx()->vsl, SLT_VCL_Log,
				"Skipped VM breakpoint at 0x%lX (debug not enabled)",
				(long) regs.rip);
		}
	}
}

} // kvm
