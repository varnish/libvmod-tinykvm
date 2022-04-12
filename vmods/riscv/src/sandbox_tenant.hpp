#pragma once
#include "script.hpp"
#include "tenant.hpp"
#include "machine_instance.hpp"
#define SCRIPT_MAGIC 0x83e59fa5

namespace rvs {

struct SandboxTenant
{
	using ghandler_t = std::function<void(Script&)>;

	Script* vmfork(const vrt_ctx*, bool debug);
	bool no_program_loaded() const noexcept { return this->program == nullptr; }

	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	void set_dynamic_call(const std::string& name, ghandler_t);
	void set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>>);
	void reset_dynamic_call(const std::string& name, ghandler_t = nullptr);
	void dynamic_call(uint32_t hash, Script&) const;

	inline Script::gaddr_t lookup(const char* name) const {
		auto ref = program;
		if (LIKELY(ref != nullptr))
			return ref->lookup(name);
		return 0x0;
	}

	inline auto callsite(const char* name) {
		auto ref = program;
		if (LIKELY(ref != nullptr)) {
			auto addr = ref->lookup(name);
			return ref->script.callsite(addr);
		}
		return decltype(ref->script.callsite(0)) {};
	}

	SandboxTenant(const vrt_ctx*, const TenantConfig&);
	static void init();
	void init_vmods(const vrt_ctx*);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable program */
	std::shared_ptr<MachineInstance> program = nullptr;
	/* Program for debugging */
	std::shared_ptr<MachineInstance> debug_program = nullptr;
	/* Hash map of string hashes associated with dyncall handlers */
	std::unordered_map<uint32_t, ghandler_t> m_dynamic_functions;
};

} // rvs
