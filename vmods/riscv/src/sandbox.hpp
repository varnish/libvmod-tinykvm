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
	int forkcall(VRT_CTX, Script::gaddr_t addr);
	Script* vmfork(VRT_CTX);
	bool no_program_loaded() const noexcept { return this->machine == nullptr; }

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

	vmod_riscv_machine(VRT_CTX, const TenantConfig&);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	std::shared_ptr<MachineInstance> machine = nullptr;
};
