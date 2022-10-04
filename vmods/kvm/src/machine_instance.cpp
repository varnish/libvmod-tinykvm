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
#include "settings.hpp"
#include "tenant_instance.hpp"
#include "timing.hpp"
#include "varnish.hpp"
extern "C" int close(int);
extern void setup_kvm_system_calls();


namespace kvm {

void MachineInstance::kvm_initialize()
{
	tinykvm::Machine::init();
	setup_kvm_system_calls();
	setup_syscall_interface();
}

MachineInstance::MachineInstance(
	const std::vector<uint8_t>& binary, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance* inst,
	bool debug)
	: m_ctx(ctx),
	  m_machine(binary, tinykvm::MachineOptions{
		.max_mem = ten->config.max_main_memory(),
		.max_cow_mem = ten->config.max_req_memory(),
		.hugepages = ten->config.hugepages(),
		.master_direct_memory_writes = false,
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_debug(debug),
	  m_is_storage(true),
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"}
{
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
		// Build stack, auxvec, envp and program arguments
		machine().setup_linux(
			{"vmod_kvm", name(), TenantConfig::guest_state_file},
			tenant().config.environ());
		// Run through main()
		machine().run( tenant().config.max_boot_time() );
		// Make sure the program is waiting for requests
		if (!is_waiting_for_requests()) {
			throw std::runtime_error("Program did not wait for requests");
		}

		// Global shared memory boundary
		uint64_t shm_boundary = shared_memory_boundary();
		if (m_global_shared_memory) shm_boundary = stack_end;

		// Make forkable (with working memory)
		// TODO: Tenant config variable for storage memory
		// Max memory here is the available CoW memory for storage
		machine().prepare_copy_on_write(
			tenant().config.max_main_memory(), shm_boundary);

		// Set new vmcall stack base lower than current RSP, in
		// order to avoid trampling stack-allocated things in main.
		auto rsp = machine().registers().rsp;
		rsp = (rsp - 128UL) & ~0xFLL; // Avoid red-zone if main is leaf
		machine().set_stack_address(rsp);
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
	const MachineInstance& source, const TenantInstance* ten, ProgramInstance* inst)
	: m_ctx(nullptr),
	  m_machine(source.machine(), tinykvm::MachineOptions{
		.max_mem = ten->config.max_main_memory(),
		.max_cow_mem = ten->config.max_req_memory(),
		.hugepages = ten->config.ephemeral_hugepages(),
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_debug(source.is_debug()),
	  m_is_storage(false),
	  m_is_ephemeral(source.is_ephemeral()),
	  m_sighandler{source.m_sighandler},
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
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
	if (this->m_is_ephemeral) {
		machine().reset_to(source.machine(), {
			.max_mem = tenant().config.max_main_memory(),
			.max_cow_mem = tenant().config.max_req_memory(),
			.reset_free_work_mem = tenant().config.limit_req_memory(),
		});
	}
	m_sighandler = source.m_sighandler;

	/* Load the fds of the source */
	m_fd.reset_and_loan(source.m_fd);
	/* Load the compiled regexes of the source */
	m_regex.reset_and_loan(source.m_regex);
	/* XXX: Todo: reset more stuff */
}

MachineInstance::~MachineInstance()
{
	this->tail_reset();
}

void MachineInstance::copy_to(uint64_t addr, const void* src, size_t len, bool zeroes)
{
	machine().copy_to_guest(addr, src, len, zeroes);
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
	/* For VMs < 4GB this works well enough. */
	return tenant().config.group.max_main_memory - shared_memory_size();
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

void MachineInstance::print(std::string_view text) const
{
	if (this->m_last_newline) {
		printf(">>> [%s] %.*s", name().c_str(), (int)text.size(), text.begin());
	} else {
		printf("%.*s", (int)text.size(), text.begin());
	}
	this->m_last_newline = (text.back() == '\n');
}
tinykvm::Machine::printer_func MachineInstance::get_vsl_printer() const
{
	/* NOTE: Guests will "always" end with newlines */
	return [this] (const char* buffer, size_t len) {
		/* Avoid wrap-around and empty log */
		if (buffer + len < buffer || len == 0)
			return;
		if (this->ctx()) {
			/* Simultaneous logging is not possible with SMP. */
			const bool smp = machine().smp_active();
			auto* vsl = this->ctx()->vsl;
			if (vsl != nullptr && !smp) {
				VSLb(vsl, SLT_VCL_Log,
					"%s says: %.*s", name().c_str(), (int)len, buffer);
				return;
			}
		}
		this->print({buffer, len});
	};
}

} // kvm
