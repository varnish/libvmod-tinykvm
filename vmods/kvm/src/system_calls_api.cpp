extern "C" {
	long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir);
	void kvm_SetCacheable(VRT_CTX, bool c);
	void kvm_SetTTL(VRT_CTX, float ttl);
}

namespace kvm {

static void syscall_register_func(vCPU& cpu, MachineInstance& inst)
{
	// Register callback function for tenant
	auto regs = cpu.registers();
	//printf("register_func: Function %lld is now 0x%llX\n",
	//	regs.rdi, regs.rsi);
	const uint64_t KERNEL_END = cpu.machine().kernel_end_address();
	if (UNLIKELY(regs.rsi < KERNEL_END)) {
		throw std::runtime_error("Invalid address for register_func provided");
	}
	inst.instance().set_entry_at(regs.rdi, regs.rsi);
	regs.rax = 0;
	cpu.set_registers(regs);
}
static void syscall_wait_for_requests(vCPU& cpu, MachineInstance& inst)
{
	if (!cpu.machine().is_forked()) {
		// Wait for events (stop machine)
		inst.wait_for_requests();
		cpu.stop();
	} else {
		throw std::runtime_error("wait_for_requests(): Cannot be called from ephemeral VM");
	}
}

static void syscall_backend_response(vCPU& cpu, MachineInstance &inst)
{
	inst.finish_backend_call();
	cpu.stop();
}

static void syscall_set_backend(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	auto* dir = inst.directors().item(regs.rdi);
	kvm_SetBackend(inst.ctx(), dir);
	regs.rax = 0;
	cpu.set_registers(regs);
}
static void syscall_set_cacheable(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	if (inst.ctx()->bo) {
		kvm_SetCacheable(inst.ctx(), regs.rdi);
		kvm_SetTTL(inst.ctx(), regs.rsi / 1000.0);
		regs.rax = 0;
	} else {
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}

static void syscall_shared_memory(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	regs.rax = inst.shared_memory_boundary();
	regs.rdx = inst.shared_memory_boundary() + inst.shared_memory_size();
	cpu.set_registers(regs);
}

static void syscall_storage_mem_shared(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	inst.machine().set_main_memory_writable(true);
	regs.rax = 0;
	cpu.set_registers(regs);
}

static void syscall_all_mem_shared(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	inst.set_global_memory_shared(true);
	regs.rax = 0;
	cpu.set_registers(regs);
}

static void syscall_storage_callb(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	if (!inst.is_storage()) {
		VirtBuffer buffers[1];
		buffers[0] = {
			.addr = (uint64_t)regs.rsi,  // src
			.len  = (uint64_t)regs.rdx   // len
		};
		regs.rax = inst.instance().storage_call(cpu.machine(),
		/*  func      buf vector    dst     dstsize */
			regs.rdi, 1, buffers, regs.rcx, regs.r8);
	} else {
		/* Prevent deadlock waiting for storage, while in storage. */
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_storage_callv(vCPU& cpu, MachineInstance& inst)
{
	auto regs = cpu.registers();
	const size_t n = regs.rsi;
	if (!inst.is_storage() && n <= 64) {
		VirtBuffer buffers[64];
		cpu.machine().copy_from_guest(buffers, regs.rdx, n * sizeof(VirtBuffer));
		regs.rax = inst.instance().storage_call(cpu.machine(),
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
	auto regs = cpu.registers();
	const uint64_t function = regs.rdi;
	const uint64_t argument = regs.rsi;
	const int      async = regs.rdx;
	const uint64_t start    = regs.rcx;
	const uint64_t period   = regs.r8;
	if (start == 0 && period == 0) {
		regs.rax = inst.instance().async_storage_call(
			async, function, argument);
	} else {
		/* XXX: Racy spam avoidance of async tasks. */
		auto *prog = &inst.instance();
		if (prog->m_timer_system.racy_count() < STORAGE_TASK_MAX_TIMERS) {
			regs.rax = prog->m_timer_system.add(
				std::chrono::milliseconds(start),
				[=](auto)
				{
					prog->async_storage_call(
						async, function, argument);
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
	auto regs = cpu.registers();
	regs.rax = inst.instance().m_timer_system.remove(regs.rdi);
	cpu.set_registers(regs);
}

static void syscall_multiprocess(vCPU& cpu, MachineInstance&)
{
	auto regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		cpu.machine().timed_smpcall(num_cpus,
			cpu.machine().mmap_allocate(num_cpus * stack_size),
			stack_size,
			(uint64_t) regs.rsi, /* func */
			2.0f, /* TODO: Tenant-property */
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
static void syscall_multiprocess_array(vCPU& cpu, MachineInstance&)
{
	auto regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		cpu.machine().timed_smpcall_array(num_cpus,
			cpu.machine().mmap_allocate(num_cpus * stack_size),
			stack_size,
			(uint64_t) regs.rsi,  /* func */
			2.0f, /* TODO: Tenant-property */
			(uint64_t) regs.rdx,  /* array */
			(uint32_t) regs.rcx); /* array_size */
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess exception: %s\n", e.what());
		regs.rax = -1;
	}
	cpu.set_registers(regs);
}
static void syscall_multiprocess_clone(vCPU& cpu, MachineInstance&)
{
	auto regs = cpu.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(cpu.machine().smp_active())) {
			printf("SMP active count: %d\n", cpu.machine().smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		cpu.machine().timed_smpcall_clone(num_cpus,
			regs.rsi,
			regs.rdx,
			2.0f, /* TODO: Tenant-property */
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
	auto regs = cpu.registers();
	try {
		/* XXX: Propagate SMP exceptions */
		cpu.machine().smp_wait();
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
		.max_memory = inst.tenant().config.max_memory(),
		.max_workmem = inst.tenant().config.max_work_memory(),
		.workmem_upper = cpu.machine().banked_memory_capacity_bytes(),
		.workmem_current = cpu.machine().banked_memory_bytes(),
	};
	auto regs = cpu.registers();
	cpu.machine().copy_to_guest(regs.rdi, &meminfo, sizeof(meminfo));
	cpu.set_registers(regs);
}

} // kvm
