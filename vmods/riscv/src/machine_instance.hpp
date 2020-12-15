#pragma once
#include "script.hpp"
#include <atomic>
#include <mutex>

struct MachineInstance
{
	MachineInstance(std::vector<uint8_t>, const vrt_ctx*, vmod_riscv_machine*);
	~MachineInstance();

	inline Script::gaddr_t lookup(const char* name) const {
		const auto& it = sym_lookup.find(name);
		if (it != sym_lookup.end()) return it->second;
		// fallback
		return script.resolve_address(name);
	}

	const std::vector<uint8_t> binary;
	Script   script;
	Script   storage;
	std::mutex storage_mtx;
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
