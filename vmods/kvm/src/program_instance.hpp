#pragma once
#include "machine_instance.hpp"
#include <atomic>
#include <mutex>
namespace tinykvm {
	struct RSPClient;
}
#define KVM_PROGRAM_MAGIC 0x50ba93c7

struct ProgramInstance
{
	using gaddr_t = MachineInstance::gaddr_t;

	ProgramInstance(std::vector<uint8_t>,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	~ProgramInstance();

	inline gaddr_t lookup(const char* name) const {
		const auto& it = sym_lookup.find(name);
		if (it != sym_lookup.end()) return it->second;
		// fallback
		return script.resolve_address(name);
	}

	const std::vector<uint8_t> binary;
	MachineInstance  script;

	MachineInstance  storage;
	std::mutex storage_mtx;
	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;
	/* Lookup tree for ELF symbol names */
	std::map<std::string, gaddr_t> sym_lookup;
};
