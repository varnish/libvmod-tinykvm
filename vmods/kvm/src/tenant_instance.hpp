#pragma once
#include "tenant.hpp"
#include "program_instance.hpp"

namespace kvm {

struct TenantInstance
{
	using ghandler_t = std::function<void(MachineInstance&)>;

	MachineInstance* vmfork(const vrt_ctx*, bool debug);
	bool no_program_loaded() const noexcept { return this->program == nullptr; }

	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	void set_dynamic_call(const std::string& name, ghandler_t);
	void set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>>);
	void reset_dynamic_call(const std::string& name, ghandler_t = nullptr);
	void dynamic_call(uint32_t hash, MachineInstance&) const;

	inline uint64_t lookup(const char* name) const {
		auto inst = program;
		if (LIKELY(inst != nullptr))
			return inst->lookup(name);
		return 0x0;
	}

	TenantInstance(const vrt_ctx*, const TenantConfig&);
	void init_vmods(const vrt_ctx*);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	std::shared_ptr<ProgramInstance> program = nullptr;
	/* Machine for debugging */
	std::shared_ptr<ProgramInstance> debug_program = nullptr;
	/* Hash map of string hashes associated with dyncall handlers */
	std::unordered_map<uint32_t, ghandler_t> m_dynamic_functions;
};

} // kvm
