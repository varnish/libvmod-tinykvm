#include "machine_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
extern void setup_kvm_system_calls();
static constexpr bool VERBOSE_ERRORS = true;

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
	const TenantInstance* ten, ProgramInstance& inst,
	bool storage, bool debug)
	: m_ctx(ctx),
	  m_machine(binary, {
		.max_mem = ten->config.max_memory(),
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_storage(storage), m_is_debug(debug),
	  m_regex     {ten->config.max_regex()},
	  m_directors {ten->config.max_backends()}
{
	machine().set_userdata<MachineInstance> (this);
	try {
		machine().setup_linux(
			{"vmod_kvm", name().c_str()},
			{"LC_TYPE=C", "LC_ALL=C", "USER=root"});
		/* Run through main() */
		machine().run();
		/* Make forkable */
		machine().prepare_copy_on_write();
		printf("Machine %s loaded\n", name().c_str());
	} catch (...) {
		fprintf(stderr,
			"Error: Machine not initialized properly: %s\n", name().c_str());
		throw; /* IMPORTANT: Re-throw */
	}
}

MachineInstance::MachineInstance(
	const MachineInstance& source, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance& inst)
	: m_ctx(ctx),
	  m_machine(source.machine(), {
		.max_mem = ten->config.max_memory(),
/*		.page_allocator = [this] (const size_t N) -> char* {
			char* mem = (char *)WS_Alloc(m_ctx->ws, (N + 1) * 4096);
			if (mem == nullptr) return nullptr;
			// Page re-alignment
			uintptr_t addr = (uintptr_t) mem;
			if (addr & ~(uint64_t) 0xFFF) {
				mem += 0x1000 - (addr & 0xFFF);
			}
			return mem;
		},
		.page_deallocator = [] (char*) {
		},*/
	  }),
	  m_tenant(ten), m_inst(inst),
	  m_is_debug(source.is_debug()),
	  m_sighandler{source.m_sighandler},
	  m_regex     {ten->config.max_regex()},
	  m_directors {ten->config.max_backends()}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	/* Load the compiled regexes of the source */
	m_regex.loan_from(source.m_regex);
	/* Load the directors of the source */
	m_directors.loan_from(source.m_directors);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Total time in MachineInstance constr body: %ldns\n", nanodiff(t0, t1));
#endif
}
void MachineInstance::reset_to(const vrt_ctx* ctx, MachineInstance& master)
{
	this->m_ctx = ctx;
	machine().reset_to(master.machine());
	/* XXX: Todo: reset more stuff */
}

MachineInstance::~MachineInstance()
{
	// free any owned regex pointers
	m_regex.foreach_owned(
		[] (auto& entry) {
			VRE_free(&entry.item);
		});
	if (this->is_debug()) {
		//this->stop_debugger();
	}
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

} // kvm
