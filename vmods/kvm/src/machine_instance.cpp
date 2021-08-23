#include "machine_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
extern "C" int close(int);
extern void setup_kvm_system_calls();

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

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
	bool storage, bool debug)
	: m_ctx(ctx),
	  m_machine(binary, {
		.max_mem = ten->config.max_memory(),
		.max_cow_mem = ten->config.max_work_memory(),
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_storage(storage), m_is_debug(debug),
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"},
	  m_directors {ten->config.max_backends(), "Directors"}
{
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
	try {
		machine().setup_linux(
			{"vmod_kvm", name(), storage ? "1" : "0"},
			{"LC_TYPE=C", "LC_ALL=C", "USER=root"});
		/* Run through main() */
		machine().run();
		if (!storage) {
			/* Make forkable */
			machine().prepare_copy_on_write();
		} else {
			printf("Program for tenant %s is loaded\n", name().c_str());
		}
	} catch (...) {
		fprintf(stderr,
			"Error: Machine not initialized properly: %s\n", name().c_str());
		throw; /* IMPORTANT: Re-throw */
	}
}

MachineInstance::MachineInstance(
	std::shared_ptr<MachineInstance>& source, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance* inst)
	: m_ctx(ctx),
	  m_machine(source->machine(), {
		.max_mem = ten->config.max_memory(),
		.max_cow_mem = ten->config.max_work_memory(),
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_storage(false),
	  m_is_debug(source->is_debug()),
	  m_sighandler{source->m_sighandler},
	  m_fd        {ten->config.max_fd(), "File descriptors"},
	  m_regex     {ten->config.max_regex(), "Regex handles"},
	  m_directors {ten->config.max_backends(), "Directors"},
	  m_mach_ref {source}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
	/* Load the fds of the source */
	m_fd.reset_and_loan(source->m_fd);
	/* Load the compiled regexes of the source */
	m_regex.reset_and_loan(source->m_regex);
	/* Load the directors of the source */
	m_directors.reset_and_loan(source->m_directors);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Total time in MachineInstance constr body: %ldns\n", nanodiff(t0, t1));
#endif
}

MachineInstance::MachineInstance(const MachineInstance& source)
	: m_ctx(source.ctx()),
	  m_machine(source.machine(), {
		.max_mem = source.tenant().config.max_memory(),
		.max_cow_mem = source.tenant().config.max_work_memory(),
		.binary = std::string_view{source.machine().binary()},
		.linearize_memory = true
	  }),
	  m_tenant(source.m_tenant), m_inst(source.m_inst),
	  m_is_storage(false),
	  m_is_debug(source.is_debug()),
	  m_sighandler{source.m_sighandler},
	  m_fd        {m_tenant->config.max_fd(), "File descriptors"},
	  m_regex     {m_tenant->config.max_regex(), "Regex handles"},
	  m_directors {m_tenant->config.max_backends(), "Directors"}
{
	machine().set_userdata<MachineInstance> (this);
	machine().set_printer(get_vsl_printer());
	/* XXX: Handle file descriptors */
	machine().prepare_copy_on_write();
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
	std::shared_ptr<MachineInstance>& source)
{
	this->m_mach_ref = source;
	this->m_ctx = ctx;
	m_tenant = source->m_tenant;
	machine().reset_to(source->machine(), {
		.max_mem = tenant().config.max_memory(),
		.max_cow_mem = tenant().config.max_work_memory(),
	});
	m_inst   = source->m_inst;
	m_sighandler = source->m_sighandler;

	/* Load the fds of the source */
	m_fd.reset_and_loan(source->m_fd);
	/* Load the compiled regexes of the source */
	m_regex.reset_and_loan(source->m_regex);
	/* Load the directors of the source */
	m_directors.reset_and_loan(source->m_directors);
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

uint64_t MachineInstance::max_time() const noexcept {
	return tenant().config.max_time();
}
const std::string& MachineInstance::name() const noexcept {
	return tenant().config.name;
}
const std::string& MachineInstance::group() const noexcept {
	return tenant().config.group.name;
}

tinykvm::Machine::printer_func MachineInstance::get_vsl_printer() const
{
	/* NOTE: Guests will "always" end with newlines */
	return [this] (const char* buffer, size_t len) {
		/* Avoid wrap-around and empty log */
		if (buffer + len < buffer || len == 0)
			return;
		auto* vsl = this->ctx()->vsl;
		if (vsl != nullptr) {
			VSLb(vsl, SLT_VCL_Log,
				"%s says: %.*s", name().c_str(), (int)len, buffer);
		} else {
			printf("%s says: %.*s", name().c_str(), (int)len, buffer);
		}
	};
}

} // kvm
