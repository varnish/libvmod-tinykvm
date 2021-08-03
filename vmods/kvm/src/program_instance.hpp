#pragma once
#include "machine_instance.hpp"
#include <map>
#include <mutex>
namespace tinykvm {
	struct RSPClient;
}

namespace kvm {
struct inst_pair {
	MachineInstance* inst;
	void (*free) (void*);
};

class ProgramInstance {
public:
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

	/* Workspace-allocated VM */
	inst_pair workspace_fork(const vrt_ctx*,
		TenantInstance*, std::shared_ptr<ProgramInstance>&);
	/* Heap-allocated VM from concurrent queue */
	inst_pair concurrent_fork(const vrt_ctx*,
		TenantInstance*, std::shared_ptr<ProgramInstance>&);

	const std::vector<uint8_t> binary;
	MachineInstance  script;

	std::unordered_map<int, MachineInstance*> instances;
	std::mutex queue_mtx;

	MachineInstance  storage;
	std::mutex storage_mtx;

	/* Lookup tree for ELF symbol names */
	std::map<std::string, gaddr_t> sym_lookup;

	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;
};

} // kvm
