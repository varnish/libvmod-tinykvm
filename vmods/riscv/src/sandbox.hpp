#pragma once
#include "script.hpp"

struct MachineInstance {
	MachineInstance(std::vector<uint8_t> elf, VRT_CTX, vmod_riscv_machine* vrm)
		: binary{std::move(elf)},
		  script{binary, ctx, vrm, *this}
	{
		extern std::vector<const char*> riscv_lookup_wishlist;
		for (const auto* func : riscv_lookup_wishlist) {
			/* NOTE: We can't check if addr is 0 here, because
			   the wishlist applies to ALL machines. */
			const auto addr = lookup(func);
			sym_lookup.emplace(strdup(func), addr);
			const auto callsite = script.callsite(addr);
			sym_vector.push_back({func, addr, callsite.size});
		}
	}

	inline Script::gaddr_t lookup(const char* name) const {
		const auto& it = sym_lookup.find(name);
		if (it != sym_lookup.end()) return it->second;
		// fallback
		return script.resolve_address(name);
	}

	const std::vector<uint8_t> binary;
	Script   script;
	/* Lookup tree for ELF symbol names */
	eastl::string_map<Script::gaddr_t,
			eastl::str_less<const char*>,
			eastl::allocator_malloc> sym_lookup;
	/* Index vector for ELF symbol names, used by call_index(..) */
	struct Lookup {
		const char* func;
		Script::gaddr_t addr;
		size_t size;
	};
	std::vector<Lookup> sym_vector;
};

struct vmod_riscv_machine {
	vmod_riscv_machine(const char* nm, const char* grp,
		std::vector<uint8_t> elf, VRT_CTX,
		uint64_t insn, uint64_t mem, uint64_t heap)
		: name(nm), group(grp),
		  max_instructions(insn), max_memory(mem), max_heap(heap),
		  machine{std::make_unique<MachineInstance> (std::move(elf), ctx, this)}
	{
		/* */
	}

	auto& script() { return machine->script; }
	inline Script::gaddr_t lookup(const char* name) const {
		return machine->lookup(name);
	}

	inline auto callsite(const char* name) {
		auto addr = machine->lookup(name);
		return script().callsite(addr);
	}

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const char*    name;
	const char*    group;
	const uint64_t max_instructions;
	const uint64_t max_memory;
	const uint64_t max_heap;
	/* Hot-swappable machine */
	std::unique_ptr<MachineInstance> machine;
	static std::vector<const char*> lookup_wishlist;
};
