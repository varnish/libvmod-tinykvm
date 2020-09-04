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

	const auto& script() const { return machine->script; }
	inline Script::gaddr_t lookup(const char* name) const {
		if (LIKELY(machine != nullptr))
			return machine->lookup(name);
		return 0x0;
	}

	inline auto callsite(const char* name) {
		auto addr = machine->lookup(name);
		return script().callsite(addr);
	}

	vmod_riscv_machine(VRT_CTX, const TenantConfig&);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	std::unique_ptr<MachineInstance> machine = nullptr;
	static std::vector<const char*> lookup_wishlist;
};
