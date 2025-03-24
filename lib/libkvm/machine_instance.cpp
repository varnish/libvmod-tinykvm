/**
 * @file machine_instance.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief TinyKVM and API wrapper for tenant programs.
 * @version 0.1
 * @date 2022-07-23
 * 
 * Responsible for wrapping the TinyKVM VMM and guest program
 * functions and API. A MachineInstance is always run inside a
 * specific thread pool with one thread, in order to guarantee
 * that it is running in the thread it was created in. This is
 * a requirement by KVM. 
 * 
**/
#include "machine_instance.hpp"
#include "paused_vm_state.hpp"
#include "settings.hpp"
#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "timing.hpp"
#include "varnish.hpp"
#include <tinykvm/util/elf.h>
extern "C" int close(int);
extern void setup_kvm_system_calls();


namespace kvm {

void MachineInstance::kvm_initialize()
{
	tinykvm::Machine::init();
	setup_kvm_system_calls();
	setup_syscall_interface();
}

static uint64_t detect_gigapage_from(const std::vector<uint8_t>& binary)
{
	if (binary.size() < 128U)
		throw std::runtime_error("Invalid ELF program (binary too small)");
	auto* elf = (Elf64_Ehdr *)binary.data();

	auto start_address_gigapage = elf->e_entry >> 30U;
	if (start_address_gigapage >= 64)
		throw std::runtime_error("Invalid ELF start address (adress was > 64GB)");

	return start_address_gigapage << 30U;
}

MachineInstance::MachineInstance(
	const std::vector<uint8_t>& binary, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance* inst,
	bool storage, bool debug)
	: m_ctx(ctx),
	  m_machine(binary, tinykvm::MachineOptions{
		.max_mem = ten->config.max_main_memory(),
		.max_cow_mem = 0UL,
		.vmem_base_address = detect_gigapage_from(binary),
		.remappings {ten->config.group.vmem_remappings},
		.verbose_loader = ten->config.group.verbose,
		.hugepages = ten->config.hugepages(),
		.master_direct_memory_writes = false,
		.relocate_fixed_mmap = ten->config.group.relocate_fixed_mmap,
		.executable_heap = ten->config.group.vmem_heap_executable,
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_debug(debug),
	  m_is_storage(storage),
	  m_print_stdout(ten->config.print_stdout()),
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"}
{
	// By default programs start out ephemeral, but it can be overridden
	this->m_is_ephemeral = ten->config.group.ephemeral;
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
}
void MachineInstance::initialize()
{
	try {
		/* Some run-times are quite buggy. Zig makes a calculation on
		   RSP and the loadable segments in order to find img_base.
		   This calculation results in a panic when the stack is
		   below the program and heap. Workaround: Move above.
		   TOOD: Make sure we have room for it, using memory limits. */
		const auto stack = machine().mmap_allocate(MAIN_STACK_SIZE);
		const auto stack_end = stack + MAIN_STACK_SIZE;
		machine().set_stack_address(stack_end);
		//printf("Heap BRK: 0x%lX -> 0x%lX\n", machine().heap_address(), machine().heap_address() + tinykvm::Machine::BRK_MAX);
		//printf("Stack: 0x%lX -> 0x%lX\n", stack, stack + MAIN_STACK_SIZE);

		// Main arguments: 3x mandatory + N configurable
		std::vector<std::string> args {
			name(), TenantConfig::guest_state_file, is_storage() ? "storage" : "request"
		};
		std::shared_ptr<std::vector<std::string>> main_arguments =
			std::atomic_load(&tenant().config.group.main_arguments);
		if (main_arguments != nullptr) {
			args.insert(args.end(), main_arguments->begin(), main_arguments->end());
		}

		// Build stack, auxvec, envp and program arguments
		machine().setup_linux(
			args,
			tenant().config.environ());

		// If verbose pagetables, print them just before running
		if (tenant().config.group.verbose_pagetable) {
			machine().print_pagetables();
		}

		if (this->is_debug()) {
			this->open_debugger(2159, 120.0f);
		}

		// Continue/resume or run through main()
		machine().run( tenant().config.max_boot_time() );

		// Make sure the program is waiting for requests
		if (!is_waiting_for_requests()) {
			throw std::runtime_error("Program did not wait for requests");
		}

		// We don't know if this is a resumable VM, but if it is we must skip
		// over the OUT instruction that was executed in the backend call.
		// We can do this regardless of whether it is a resumable VM or not.
		// This will also help make faulting VMs return back to the correct
		// state when they are being reset.
		auto& regs = machine().registers();
		regs.rip += 2;
		machine().set_registers(regs);

		// Only request VMs need the copy-on-write mechanism enabled
		if (!is_storage())
		{
			// This can probably be solved later, but for now they are incompatible
			if (shared_memory_size() > 0 && !tenant().config.group.vmem_remappings.empty())
			{
				throw std::runtime_error("Shared memory is currently incompatible with vmem remappings");
			}
			// Global shared memory boundary
			uint64_t shm_boundary = shared_memory_boundary();
			//printf("Shared memory boundary: 0x%lX Max addr: 0x%lX\n",
			//	shm_boundary, machine().max_address());

			// Make forkable (with *NO* working memory)
			machine().prepare_copy_on_write(0UL, shm_boundary);
		}

		// Set new vmcall stack base lower than current RSP, in
		// order to avoid trampling stack-allocated things in main.
		auto rsp = machine().registers().rsp;
		if (rsp >= stack && rsp < stack_end) {
			rsp = (rsp - 128UL) & ~0xFLL; // Avoid red-zone if main is leaf
			machine().set_stack_address(rsp);
		}
	}
	catch (const tinykvm::MachineException& me)
	{
		fprintf(stderr,
			"Machine not initialized properly: %s\n", name().c_str());
		fprintf(stderr,
			"Error: %s Data: 0x%#lX\n", me.what(), me.data());
		throw; /* IMPORTANT: Re-throw */
	}
	catch (const std::exception& e)
	{
		fprintf(stderr,
			"Machine not initialized properly: %s\n", name().c_str());
		fprintf(stderr,
			"Error: %s\n", e.what());
		throw; /* IMPORTANT: Re-throw */
	}
}

MachineInstance::MachineInstance(
	unsigned reqid,
	const MachineInstance& source, const TenantInstance* ten, ProgramInstance* inst)
	: m_ctx(nullptr),
	  m_machine(source.machine(), tinykvm::MachineOptions{
		.max_mem = ten->config.max_main_memory(),
		.max_cow_mem = ten->config.max_req_memory(),
		.reset_free_work_mem = ten->config.limit_req_memory(),
		.remappings {},
		.hugepages = ten->config.ephemeral_hugepages(),
		.relocate_fixed_mmap = ten->config.group.relocate_fixed_mmap,
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_debug(source.is_debug()),
	  m_is_storage(false),
	  m_is_ephemeral(source.is_ephemeral()),
	  m_waiting_for_requests(true), // If we got this far, we are waiting...
	  m_sighandler{source.m_sighandler},
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
	/* vCPU request id */
	machine().cpu().set_vcpu_table_at(1, reqid);
	/* Load the fds of the source */
	m_fd.reset_and_loan(source.m_fd);
	/* Load the compiled regexes of the source */
	m_regex.reset_and_loan(source.m_regex);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Total time in MachineInstance constr body: %ldns\n", nanodiff(t0, t1));
#endif
}

void MachineInstance::tail_reset()
{
	/* Close any open files */
	m_fd.foreach_owned(
		[] (const auto& entry) {
			close(entry.item);
		});
	/* Free any owned regex pointers */
	m_regex.foreach_owned(
		[] (auto& entry) {
			VRE_free(&entry.item);
		});
	if (this->is_debug()) {
		//this->stop_debugger();
	}
}
void MachineInstance::reset_to(const vrt_ctx* ctx,
	MachineInstance& source)
{
	this->m_ctx = ctx;

	/* If it crashed, or reset is always needed, then reset now. */
	const bool reset_needed = this->m_reset_needed || this->m_is_ephemeral;

	/* We only reset ephemeral VMs. */
	if (reset_needed) {
		this->m_reset_needed = false;

		stats().resets ++;
		machine().reset_to(source.machine(), {
			.max_mem = tenant().config.max_main_memory(),
			.max_cow_mem = tenant().config.max_req_memory(),
			.reset_free_work_mem = tenant().config.limit_req_memory(),
			.remappings {},
		});
		this->m_waiting_for_requests = source.m_waiting_for_requests;
		/* The POST memory area is gone. */
		this->m_post_size = 0;
		/* The ephemeral backend_inputs "stack" area is gone. */
		this->m_inputs_allocation = 0;

		m_sighandler = source.m_sighandler;

		/* Load the fds of the source */
		m_fd.reset_and_loan(source.m_fd);
		/* Load the compiled regexes of the source */
		m_regex.reset_and_loan(source.m_regex);
		/* XXX: Todo: reset more stuff */
	}
}

MachineInstance::~MachineInstance()
{
	this->tail_reset();
}

void MachineInstance::wait_for_requests_paused()
{
	this->m_waiting_for_requests = true;

	/* Only ephemeral VMs need PausedVMState */
	if (this->tenant().config.group.ephemeral) {
		std::any& state = program().get_paused_vm_state();
		auto& pvs = state.emplace<PausedVMState>(
			PausedVMState{machine().registers(), machine().fpu_registers()});
		pvs.regs.rflags = 2 | (3 << 12);
		pvs.regs.rip += 2; // Skip the OUT instruction
	}
	//printf("*** Waiting for requests in paused state\n");
}

void MachineInstance::copy_to(uint64_t addr, const void* src, size_t len, bool zeroes)
{
	machine().copy_to_guest(addr, src, len, zeroes);
}

bool MachineInstance::allows_debugging() const noexcept
{
	return tenant().config.group.allow_debug;
}
float MachineInstance::max_req_time() const noexcept {
	return tenant().config.max_req_time(is_debug());
}
const std::string& MachineInstance::name() const noexcept {
	return tenant().config.name;
}
const std::string& MachineInstance::group() const noexcept {
	return tenant().config.group.name;
}

uint64_t MachineInstance::shared_memory_boundary() const noexcept
{
	if (shared_memory_size() > 0)
		/* For VMs < 4GB this works well enough. */
		return tenant().config.group.max_main_memory - shared_memory_size();
	else
		return ~uint64_t(0);
}
uint64_t MachineInstance::shared_memory_size() const noexcept
{
	return tenant().config.group.shared_memory;
}

void MachineInstance::print_backtrace()
{
	const auto regs = machine().registers();
	machine().print_registers();

	uint64_t rip = regs.rip;
	if (rip >= 0x2000 && rip < 0x3000) {
		/* Exception handler */
		try {
			machine().unsafe_copy_from_guest(&rip, regs.rsp, 8);
			// Unwinding the stack is too hard :(
			// But this is the real RSP:
			//machine().unsafe_copy_from_guest(&rsp, regs.rsp + 24, 8);
		} catch (...) {}
	}

	char buffer[4096];
	int len = snprintf(buffer, sizeof(buffer),
		"[0] 0x%8lX   %s\n",
		rip, machine().resolve(rip).c_str());
	if (len > 0) {
		machine().print(buffer, len);
	}
}

const vrt_ctx* MachineInstance::ctx() const
{
	if (UNLIKELY(m_ctx == nullptr))
		throw std::runtime_error("CTX was null");
	return m_ctx;
}

uint64_t MachineInstance::allocate_post_data(size_t bytes)
{
	/* Simple mremap scheme. */
	if (this->m_post_size < bytes)
	{
		if (this->m_post_size > 0)
			machine().mmap_unmap(m_post_data, m_post_size);

		this->m_post_data = machine().mmap_allocate(bytes);
		this->m_post_size = bytes;
	}
	return this->m_post_data;
}

void MachineInstance::print(std::string_view text) const
{
	if (this->m_last_newline) {
		printf(">>> [%s] %.*s", name().c_str(), (int)text.size(), text.begin());
	} else {
		printf("%.*s", (int)text.size(), text.begin());
	}
	this->m_last_newline = (text.back() == '\n');
}
void MachineInstance::logprint(std::string_view text, bool says) const
{
	/* Simultaneous logging is not possible with SMP. */
	const bool smp = machine().smp_active();
	if (!smp && this->has_ctx() && this->ctx()->vsl) {
		auto* vsl = this->ctx()->vsl;
		if (says) {
			VSLb(vsl, SLT_VCL_Log,
				"%s says: %.*s", name().c_str(), (int)text.size(), text.begin());
		} else {
			VSLb(vsl, SLT_VCL_Log,
				"%.*s", (int)text.size(), text.begin());
		}
	} else {
		if (says) {
			printf(">>> [%s] %.*s", name().c_str(), (int)text.size(), text.begin());
		} else {
			printf("%.*s", (int)text.size(), text.begin());
		}
	}
}
tinykvm::Machine::printer_func MachineInstance::get_vsl_printer() const
{
	/* NOTE: Guests will "always" end with newlines */
	return [this] (const char* buffer, size_t len) {
		/* Avoid wrap-around and empty log */
		if (buffer + len < buffer || len == 0)
			return;
		/* Logging with $PROGRAM says: ... */
		this->logprint(std::string_view(buffer, len), true);

		if (this->m_print_stdout) {
			this->print({buffer, len});
		}
	};
}

} // kvm
