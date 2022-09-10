#pragma once
#include "machine_instance.hpp"
#include "instance_cache.hpp"
#include "settings.hpp"
#include "adns.hpp"
#include "utils/cpptime.hpp"
#include <blockingconcurrentqueue.h>
struct vcl;

namespace kvm {
struct VirtBuffer {
	uint64_t addr;
	size_t   len;
};

/**
 * A pool item is all the bits necessary to execute inside KVM
 * for a particular tenant. The pool item can be requested as
 * part of a front or backend request. It guarantees ownership
 * of a VM until a request is fully completed, including any
 * data transfers, and is then put back in a blocking queue.
**/
struct VMPoolItem {
	VMPoolItem(const MachineInstance&, const vrt_ctx*, TenantInstance*, ProgramInstance*);
	// VM instance
	std::unique_ptr<MachineInstance> mi;
	// Communicate with this VM using single thread pool
	tinykvm::ThreadPool tp {1, REQUEST_VM_NICE, false};
	// Reference that keeps active program alive
	std::shared_ptr<ProgramInstance> prog_ref = nullptr;
};
struct Reservation {
	VMPoolItem* slot;
	void (*free) (void*);
};

enum class ProgramEntryIndex : uint8_t {
	ON_RECV = 0,
	BACKEND_COMP = 1,
	BACKEND_POST = 2,
	BACKEND_STREAM = 3,
	LIVEUPD_SERIALIZE = 4,
	LIVEUPD_DESERIALIZE = 5,
	ON_ERROR = 6,
	TOTAL_ENTRIES
};

/**
 * ProgramInstance is a collection of machines, thread pools
 * and state that will be replaced each time a new program
 * is sent to this tenant. The original program is stored
 * alongside the VMs, but the VMs can diverge greatly over
 * time. A ProgramInstance is kept alive for the duration of
 * all requests that are using it. Once they are all finished
 * a ProgramInstance may be destroyed if a live update happened
 * during execution. Programs are kept alive until everything
 * related to a request is completed, including data transfers.
 * That is why you may see a lot of reference counting. There
 * is additional reference counting on the main VM, as it may
 * be live updated using a vmcommit system call.
**/
class ProgramInstance {
public:
	using gaddr_t = MachineInstance::gaddr_t;
	static constexpr size_t ADNS_MAX = 8;

	ProgramInstance(std::vector<uint8_t> binary,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	ProgramInstance(const std::string& uri,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	~ProgramInstance();
	long wait_for_initialization();

	/* Look up the address of the given name (function or object)
	   in the currently running program. The operation is very
	   expensive as it iterates through the ELF symbol table. */
	gaddr_t lookup(const char* name) const;

	/* Entries are function addresses in an array that belongs
	   to the running program. The program self-registers callbacks
	   used by Varnish during various stages, such as GET and POST.
	   We don't need to know function names because the function just
	   directly registers the address of the function.

	   See: ProgramEntryIndex for what each entry means. */
	gaddr_t entry_at(int) const;
	gaddr_t entry_at(ProgramEntryIndex i) const { return entry_at((int) i); }
	void set_entry_at(int, gaddr_t);
	void set_entry_at(ProgramEntryIndex i, gaddr_t a) { return set_entry_at((int) i, a); }

	/* Reserve VM from blocking queue. */
	Reservation reserve_vm(const vrt_ctx*,
		TenantInstance*, std::shared_ptr<ProgramInstance>);

	/* Serialized vector-based vmcall into storage VM.
	   NOTE: buffers are clobbered by the function call. */
	long storage_call(tinykvm::Machine& src,
		gaddr_t func, size_t n, VirtBuffer[], gaddr_t, size_t);

	/* Async serialized vmcall into storage VM. */
	long async_storage_call(bool async, gaddr_t func, gaddr_t arg);

	/* Serialized call into storage VM during live update */
	long live_update_call(const vrt_ctx*,
		gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc);

	std::vector<uint8_t> binary;
	/* Ready-made _main_ VM that can be forked into many small VMs */
	std::unique_ptr<MachineInstance> main_vm;
	/* Extra vCPU used for async storage tasks */
	std::unique_ptr<tinykvm::vCPU> main_vm_extra_cpu = nullptr;
	uint64_t main_vm_extra_cpu_stack = 0x0;

	/* Ticket-machine that gives access rights to VMs. */
	moodycamel::BlockingConcurrentQueue<VMPoolItem*> m_vmqueue;
	/* Simple container for VMs. */
	std::deque<VMPoolItem> m_vms;

	/* Tasks executed in storage outside an active request. */
	std::deque<std::future<long>> m_async_tasks;
	std::mutex m_async_mtx;

	/* Queue of work to happen on main VM. Bottleneck. */
	tinykvm::ThreadPool m_main_queue;
	/* Separate queue for async calls into main VM. */
	tinykvm::ThreadPool m_main_async_queue;

	/* Entry points in the tenants program. Handlers for all types of
	   requests, serialization mechanisms and related functionality. */
	std::array<gaddr_t, (size_t)ProgramEntryIndex::TOTAL_ENTRIES> entry_address {};

	/* The timer system needs to be destroyed before any of the other
	   things, like the storage and request VMs. Timers carry capture
	   storage referring to the other program members. */
	cpptime::TimerSystem m_timer_system;

	/* libadns keyes on VCL structs. */
	vcl* get_adns_key() const { return this->m_vcl; }
	vcl* m_vcl;

	/* ADNS tags are used to receive DNS changes over time (storage only).
	   Initialized during storage initialization, read-only during request handling. */
	std::array<AsyncDNS, ADNS_MAX> m_adns_tags;
	int free_adns_tag() const { for (size_t i = 0; i < m_adns_tags.size(); i++)if (m_adns_tags[i].tag.empty()) return i; return -1; }

	/* Live debugging feature using the GDB RSP protocol.
	   Debugging allows stepping through the tenants program line by line
	   as the request is processed. Once the debugger is connected KVM is
	   switched to single stepping mode, and opens up the ability to connect
	   to the Varnish instance using remote GDB. Once the connection is
	   concluded KVM will continue running, finishing the request normally. */
	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;

	bool initialization_complete = false;
private:
	long begin_initialization(const vrt_ctx *, TenantInstance *, bool debug);
	/* Wait for Varnish to listen and this program to complete initialization. */
	void try_wait_for_startup_and_initialization();

	std::future<long> m_future;
	std::mutex mtx_future_init;
};

} // kvm
