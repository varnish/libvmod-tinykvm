#pragma once
#include "machine_instance.hpp"
#include "utils/thread_pool.hpp"
namespace tinykvm {
	struct RSPClient;
}

namespace kvm {
struct inst_pair {
	MachineInstance* inst;
	void (*free) (void*);
};
struct VirtBuffer {
	uint64_t addr;
	size_t   len;
};

class ProgramInstance {
public:
	using gaddr_t = MachineInstance::gaddr_t;

	ProgramInstance(std::vector<uint8_t>,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	ProgramInstance(const MachineInstance&);
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

	/* Serialized vector-based vmcall into storage VM */
	long storage_call(tinykvm::Machine& src,
		gaddr_t func, size_t n, VirtBuffer[n], gaddr_t, size_t);

	/* Serialized call into storage VM during live update */
	long live_update_call(gaddr_t func, tinykvm::Machine& newm, gaddr_t newfunc);

	const std::vector<uint8_t> binary;
	MachineInstance  script;

	MachineInstance  storage;
	kvm::ThreadPool<1> m_storage_queue;

	/* Lookup tree for ELF symbol names */
	std::unordered_map<std::string, gaddr_t> sym_lookup;

	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;
};

} // kvm
