#pragma once
#include "machine_instance.hpp"
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
enum class ProgramEntryIndex : uint8_t {
	ON_RECV = 0,
	BACKEND_COMP = 1,
	BACKEND_POST = 2,
	BACKEND_STREAM = 3,
	LIVEUPD_SERIALIZE = 4,
	LIVEUPD_DESERIALIZE = 5,
	TOTAL_ENTRIES
};

class ProgramInstance {
public:
	using gaddr_t = MachineInstance::gaddr_t;

	ProgramInstance(std::vector<uint8_t>,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	~ProgramInstance();

	gaddr_t lookup(const char* name) const;
	gaddr_t entry_at(int) const;
	gaddr_t entry_at(ProgramEntryIndex i) const { return entry_at((int) i); }
	void set_entry_at(int, gaddr_t);
	void set_entry_at(ProgramEntryIndex i, gaddr_t a) { return set_entry_at((int) i, a); }

	/* Thread-local heap-allocated VM. */
	inst_pair concurrent_fork(const vrt_ctx*,
		TenantInstance*, std::shared_ptr<ProgramInstance>&);

	/* Serialized vector-based vmcall into storage VM.
	   NOTE: buffers are clobbered by the function call. */
	long storage_call(tinykvm::Machine& src,
		gaddr_t func, size_t n, VirtBuffer[], gaddr_t, size_t);

	/* Async serialized vmcall into storage VM. */
	long async_storage_call(gaddr_t func, gaddr_t arg);

	/* Serialized call into storage VM during live update */
	long live_update_call(const vrt_ctx*,
		gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc);

	void commit_instance_live(
		std::shared_ptr<MachineInstance>& new_inst);

	const std::vector<uint8_t> binary;
	std::shared_ptr<MachineInstance> script;

	MachineInstance  storage;
	tinykvm::ThreadPool m_storage_queue;
	std::vector<std::future<long>> m_async_tasks;

	std::array<gaddr_t, (size_t)ProgramEntryIndex::TOTAL_ENTRIES> entry_address;

	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;
};

} // kvm
