#pragma once
#include "script.hpp"
#include <atomic>

struct MachineInstance
{
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

	void add_reference() const;
	void remove_reference() const;

	const std::vector<uint8_t> binary;
	Script   script;
	mutable std::atomic<unsigned> refcount = 1;
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
