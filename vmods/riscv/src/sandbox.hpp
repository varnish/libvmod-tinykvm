#pragma once
#include "script.hpp"
#include "machine_instance.hpp"

struct vmod_riscv_machine {
	vmod_riscv_machine(const char* nm, const char* grp,
		const char* filename, VRT_CTX,
		uint64_t insn, uint64_t mem, uint64_t heap);

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

	bool no_program_loaded() const noexcept { return this->machine == nullptr; }

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const char*    name;
	const char*    group;
	const char*    filename;
	const uint64_t max_instructions;
	const uint64_t max_memory;
	const uint64_t max_heap;
	/* Hot-swappable machine */
	std::unique_ptr<MachineInstance> machine = nullptr;
	static std::vector<const char*> lookup_wishlist;
};
