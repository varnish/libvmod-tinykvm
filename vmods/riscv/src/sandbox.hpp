#pragma once
#include "script.hpp"

struct vmod_riscv_machine {
	vmod_riscv_machine(const char* name, std::vector<uint8_t> elf,
		VRT_CTX, uint64_t insn, uint64_t mem, uint64_t heap)
		: binary{std::move(elf)}, name(name),
		  max_instructions(insn), max_memory(mem), max_heap(heap),
		  script{binary, ctx, this}
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
	inline auto callsite(const char* name) {
		auto addr = lookup(name);
		return script.callsite(addr);
	}

	const uint64_t magic = 0xb385716f486938e6;
	const std::vector<uint8_t> binary;
	const char*    name;
	const uint64_t max_instructions;
	const uint64_t max_memory;
	const uint64_t max_heap;
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

	static std::vector<const char*> lookup_wishlist;
};
