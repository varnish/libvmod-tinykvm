#pragma once
#include "script.hpp"
#include <atomic>
#include <mutex>
namespace riscv {
	template <int W> struct RSPClient;
}

struct MachineInstance
{
	MachineInstance(std::vector<uint8_t>,
		const vrt_ctx*, SandboxTenant*, bool = false);
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
	std::unique_ptr<riscv::RSPClient<Script::MARCH>> rspclient;
	Script* rsp_script = nullptr;
	std::mutex rsp_mtx;
	/* Lookup tree for ELF symbol names */
	std::map<std::string, Script::gaddr_t> sym_lookup;
	/* Index vector for ELF symbol names, used by call_index(..) */
	struct Lookup {
		const char* func;
		Script::gaddr_t addr;
		size_t size;
	};
	std::vector<Lookup> sym_vector;
};
