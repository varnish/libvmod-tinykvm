#pragma once
#include "script.hpp"
#include "machine_instance.hpp"
#define SCRIPT_MAGIC 0x83e59fa5

struct TenantConfig
{
	std::string    name;
	std::string    group;
	std::string    filename;
	uint64_t max_instructions;
	uint64_t max_memory;
	uint64_t max_heap;
};

struct vmod_riscv_machine
{
	using ghandler_t = std::function<void(Script&)>;

	int forkcall(const vrt_ctx*, Script::gaddr_t addr);
	Script* vmfork(const vrt_ctx*);
	bool no_program_loaded() const noexcept { return this->machine == nullptr; }

	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	void set_dynamic_call(const std::string& name, ghandler_t);
	void set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>>);
	void reset_dynamic_call(const std::string& name, ghandler_t = nullptr);
	void dynamic_call(uint32_t hash, Script&) const;

	inline Script::gaddr_t lookup(const char* name) const {
		auto program = machine;
		if (LIKELY(program != nullptr))
			return program->lookup(name);
		return 0x0;
	}

	inline auto callsite(const char* name) {
		auto program = machine;
		if (LIKELY(program != nullptr)) {
			auto addr = program->lookup(name);
			return program->script.callsite(addr);
		}
		return decltype(program->script.callsite(0)) {};
	}

	vmod_riscv_machine(const vrt_ctx*, const TenantConfig&);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	std::shared_ptr<MachineInstance> machine = nullptr;
	/* Hash map of string hashes associated with dyncall handlers */
	std::unordered_map<uint32_t, ghandler_t> m_dynamic_functions;
};
