extern "C" {
	long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir);
	void kvm_SetCacheable(VRT_CTX, bool c);
	void kvm_SetTTL(VRT_CTX, float ttl);
}

namespace kvm {

static void syscall_register_func(Machine& machine, MachineInstance& inst)
{
	// Register callback function for tenant
	auto regs = machine.registers();
	//printf("register_func: Function %lld is now 0x%llX\n",
	//	regs.rdi, regs.rsi);
	inst.instance().set_entry_at(regs.rdi, regs.rsi);
	regs.rax = 0;
	machine.set_registers(regs);
}
static void syscall_wait_for_requests(Machine& machine, MachineInstance& inst)
{
	if (!machine.is_forked()) {
		// Wait for events (stop machine)
		inst.wait_for_requests();
		machine.stop();
	} else {
		throw std::runtime_error("wait_for_requests(): Cannot be called from ephemeral VM");
	}
}

static void syscall_set_backend(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	auto* dir = inst.directors().item(regs.rdi);
	kvm_SetBackend(inst.ctx(), dir);
	regs.rax = 0;
	machine.set_registers(regs);
}
static void syscall_set_cacheable(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	if (inst.ctx()->bo) {
		kvm_SetCacheable(inst.ctx(), regs.rdi);
		kvm_SetTTL(inst.ctx(), regs.rsi / 1000.0);
		regs.rax = 0;
	} else {
		regs.rax = -1;
	}
	machine.set_registers(regs);
}

static void syscall_shared_memory(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	regs.rax = inst.shared_memory_boundary();
	regs.rdx = inst.shared_memory_size();
	machine.set_registers(regs);
}

static void syscall_storage_mem_shared(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	inst.machine().set_main_memory_writable(true);
	regs.rax = 0;
	machine.set_registers(regs);
}

static void syscall_all_mem_shared(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	inst.set_global_memory_shared(true);
	regs.rax = 0;
	machine.set_registers(regs);
}

static void syscall_storage_callb(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	VirtBuffer buffers[1];
	buffers[0] = {
		.addr = (uint64_t)regs.rsi,  // src
		.len  = (uint64_t)regs.rdx   // len
	};
	regs.rax = inst.instance().storage_call(machine,
	/*  func      buf vector    dst     dstsize */
		regs.rdi, 1, buffers, regs.rcx, regs.r8);
	machine.set_registers(regs);
}
static void syscall_storage_callv(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	const size_t n = regs.rsi;
	if (n <= 64) {
		VirtBuffer buffers[64];
		machine.copy_from_guest(buffers, regs.rdx, n * sizeof(VirtBuffer));
		regs.rax = inst.instance().storage_call(machine,
		/*  func      buf vector    dst     dstsize */
			regs.rdi, n, buffers, regs.rcx, regs.r8);
	} else {
		regs.rax = -1;
	}
	machine.set_registers(regs);
}
static void syscall_storage_task(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	regs.rax = inst.instance().async_storage_call(
	/*  func      argument */
		regs.rdi, regs.rsi);
	machine.set_registers(regs);
}

static void syscall_multiprocess(Machine& machine, MachineInstance&)
{
	auto regs = machine.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(machine.smp_active())) {
			printf("SMP active count: %d\n", machine.smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		machine.timed_smpcall(num_cpus,
			machine.mmap_allocate(num_cpus * stack_size),
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
	machine.set_registers(regs);
}
static void syscall_multiprocess_array(Machine& machine, MachineInstance&)
{
	auto regs = machine.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(machine.smp_active())) {
			printf("SMP active count: %d\n", machine.smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		const size_t stack_size = 512 * 1024ul;
		machine.timed_smpcall_array(num_cpus,
			machine.mmap_allocate(num_cpus * stack_size),
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
	machine.set_registers(regs);
}
static void syscall_multiprocess_clone(Machine& machine, MachineInstance&)
{
	auto regs = machine.registers();
	try {
		/* It's too expensive to schedule multiple workloads. */
		if (UNLIKELY(machine.smp_active())) {
			printf("SMP active count: %d\n", machine.smp_active_count());
			throw std::runtime_error("Multiprocessing: Already active");
		} else if (UNLIKELY(regs.rdi < 2)) {
			throw std::runtime_error("Multiprocessing: Must request at least one vCPU");
		} else if (UNLIKELY(regs.rdi > 16)) {
			/* TODO: Tenant-property */
			throw std::runtime_error("Multiprocessing: Too many vCPUs requested");
		}
		const size_t num_cpus = regs.rdi - 1;
		machine.timed_smpcall_clone(num_cpus,
			regs.rsi,
			regs.rdx,
			2.0f, /* TODO: Tenant-property */
			regs);
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess exception: %s\n", e.what());
		regs.rax = -1;
	}
	machine.set_registers(regs);
}
static void syscall_multiprocess_wait(Machine& machine, MachineInstance&)
{
	auto regs = machine.registers();
	try {
		/* XXX: Propagate SMP exceptions */
		machine.smp_wait();
		regs.rax = 0;
	} catch (const std::exception& e) {
		fprintf(stderr, "Multiprocess wait exception: %s\n", e.what());
		regs.rax = -1;
	}
	machine.set_registers(regs);
}

static void syscall_memory_info(Machine &machine, MachineInstance &inst)
{
	const struct {
		uint64_t max_memory;
		uint64_t max_workmem;
		uint64_t workmem_upper;
		uint64_t workmem_current;
	} meminfo {
		.max_memory = inst.tenant().config.max_memory(),
		.max_workmem = inst.tenant().config.max_work_memory(),
		.workmem_upper = machine.banked_memory_capacity_bytes(),
		.workmem_current = machine.banked_memory_bytes(),
	};
	auto regs = machine.registers();
	machine.copy_to_guest(regs.rdi, &meminfo, sizeof(meminfo));
	machine.set_registers(regs);
}

} // kvm
