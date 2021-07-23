#include "machine_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
static constexpr uint64_t SIGHANDLER_INSN = 60'000;
static constexpr bool VERBOSE_ERRORS = true;

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

MachineInstance::MachineInstance(
	const MachineInstance& source, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance& inst)
	: m_machine(source.machine(), {
		.max_mem = 0,
	  }),
	  m_ctx(ctx), m_tenant(ten), m_inst(inst),
	  m_is_debug(source.is_debug()),
	  m_sighandler{source.m_sighandler},
	  m_regex     {ten->config.max_regex()},
	  m_directors {ten->config.max_backends()}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	/* No initialization */
	machine().setup_linux(
		{"vmod_kvm", "Hello KVM World!\n"},
		{"LC_TYPE=C", "LC_ALL=C", "USER=root"});

	/* Load the compiled regexes of the source */
	m_regex.loan_from(source.m_regex);
	/* Load the directors of the source */
	m_directors.loan_from(source.m_directors);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Total time in MachineInstance constr body: %ldns\n", nanodiff(t0, t1));
#endif
}

MachineInstance::MachineInstance(
	const std::vector<uint8_t>& binary, const vrt_ctx* ctx,
	const TenantInstance* ten, ProgramInstance& inst,
	bool storage, bool debug)
	: m_machine(binary, {
		.max_mem = ten->config.max_memory(),
	  }),
	  m_ctx(ctx), m_tenant(ten), m_inst(inst),
	  m_is_storage(storage), m_is_debug(debug),
	  m_regex     {ten->config.max_regex()},
	  m_directors {ten->config.max_backends()}
{
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

__attribute__((constructor))
void MachineInstance::kvm_initialize()
{
	tinykvm::Machine::init();
	extern void setup_kvm_system_calls();
	setup_kvm_system_calls();
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
