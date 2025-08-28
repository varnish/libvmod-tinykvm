#pragma once
#include "binary_storage.hpp"
#include "instance_cache.hpp"
#include "machine_instance.hpp"
#include "settings.hpp"
#include "server/epoll.hpp"
#include "server/websocket.hpp"
#include "utils/cpptime.hpp"
#include <blockingconcurrentqueue.h>
#include <tinykvm/util/threadpool.h>
#include <tinykvm/util/threadtask.hpp>
#include <unordered_set>
#include <variant>
struct vcl;
struct vsl_log;
#ifdef VARNISH_PLUS
typedef void (*priv_task_free_func_t) (void*);
#else
typedef void (*priv_task_free_func_t) (const vrt_ctx *, void*);
#endif

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
	VMPoolItem(unsigned reqid, const MachineInstance&, TenantInstance*, ProgramInstance*);
	// VM instance
	std::unique_ptr<MachineInstance> mi;
	// Reference that keeps active program alive
	std::shared_ptr<ProgramInstance> prog_ref = nullptr;
	// Communicate with this VM using single thread pool
	tinykvm::ThreadTask<tinykvm::Function<long()>> tp;
	// We can use this to avoid having to start in a serialized manner
	std::future<long> task_future;
};

struct Reservation {
	VMPoolItem* slot;
	priv_task_free_func_t free;
};

enum class ProgramEntryIndex : uint8_t {
	ON_RECV = 0,
	BACKEND_GET  = 1,
	BACKEND_POST = 2,
	BACKEND_METHOD = 3,
	BACKEND_STREAM = 4,
	BACKEND_ERROR  = 5,
	LIVEUPD_SERIALIZE = 6,
	LIVEUPD_DESERIALIZE = 7,

	SOCKET_PAUSE_RESUME_API = 12,

	TOTAL_ENTRIES
};

class Storage {
public:
	Storage(BinaryStorage storage_elf);

	/* Ready-made *storage* VM that provides a shared mutable storage */
	std::unique_ptr<MachineInstance> storage_vm;

	BinaryStorage storage_binary;

	/* Tasks executed in storage outside an active request. */
	std::deque<std::future<long>> m_async_tasks;
	std::mutex m_async_mtx;

	std::unordered_set<uint64_t> allow_list;

	bool is_allowed(uint64_t address) const noexcept {
		return allow_list.empty() || (allow_list.count(address) != 0);
	}
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

	ProgramInstance(
		BinaryStorage request_binary,
		BinaryStorage storage_binary,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	ProgramInstance(const std::string& uri, std::string ifmodsince,
		const vrt_ctx*, TenantInstance*, bool debug = false);
	~ProgramInstance();
	long wait_for_initialization();
	bool is_initialized() const noexcept { return this->m_initialization_complete > 0; }

	bool binary_was_local() const noexcept { return m_binary_was_local; }
	bool binary_was_cached() const noexcept { return m_binary_was_cached; }

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

	/* Returns true immediately if the main VM initialized successfully.
	   If initialization has not completed, wait. Returns the result of
	   the initialization. */
	bool wait_for_main_vm();

	/* Reserve VM from blocking queue. */
	Reservation reserve_vm(const vrt_ctx*,
		TenantInstance*, std::shared_ptr<ProgramInstance>);
	/* Free a reserved VM. This can potentially finish a program. */
#ifdef VARNISH_PLUS
	static void vm_free_function(void*);
#else
	static void vm_free_function(const vrt_ctx*, void*);
#endif

	/* Serialized vector-based vmcall into storage VM.
	   NOTE: buffers are clobbered by the function call. */
	long storage_call(tinykvm::Machine& src,
		gaddr_t func, size_t n, VirtBuffer[], gaddr_t, size_t);

	/* Async serialized vmcall into storage VM. */
	long storage_task(gaddr_t func, std::string argument);

	/* Serialized call into storage VM during live update */
	long live_update_call(const vrt_ctx*,
		gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc);

	/* Binary storage, either in-memory or mmap'ed file */
	BinaryStorage request_binary;

	/* Ready-made *request* VM that can be forked into many small VMs */
	std::unique_ptr<MachineInstance> main_vm;

	/* Ticket-machine that gives access rights to VMs. */
	std::array<moodycamel::BlockingConcurrentQueue<VMPoolItem*>, 4> m_vmqueue;
	/* Simple container for VMs. */
	std::deque<VMPoolItem> m_vms;

	std::unique_ptr<Storage> m_storage = nullptr;
	bool has_storage() const noexcept { return m_storage != nullptr; }
	inline Storage& storage() {
		if (m_storage) return *m_storage;
		throw std::runtime_error("Storage not initialized");
	}
	/* Queue of work to happen on storage VM. Serialized access. */
	tinykvm::ThreadTask<std::function<long()>> m_storage_queue;

	/* Entry points in the tenants program. Handlers for all types of
	   requests, serialization mechanisms and related functionality.
	   NOTE: Limiting the entries to lower 32-bits, for now. */
	std::array<uint32_t, (size_t)ProgramEntryIndex::TOTAL_ENTRIES> entry_address {};

	/* The timer system needs to be destroyed before any of the other
	   things, like the storage and request VMs. Timers carry capture
	   storage referring to the other program members. */
	cpptime::TimerSystem m_timer_system;


	/* Live debugging feature using the GDB RSP protocol.
	   Debugging allows stepping through the tenants program line by line
	   as the request is processed. Once the debugger is connected KVM is
	   switched to single stepping mode, and opens up the ability to connect
	   to the Varnish instance using remote GDB. Once the connection is
	   concluded KVM will continue running, finishing the request normally. */
	std::unique_ptr<tinykvm::RSPClient> rspclient;
	MachineInstance* rsp_script = nullptr;
	std::mutex rsp_mtx;

	struct Stats {
		uint64_t reservation_timeouts = 0;
		uint64_t live_updates = 0;
		int64_t  live_update_transfer_bytes = 0;
	} stats;

	static int numa_node();

private:
	void begin_initialization(const vrt_ctx *, TenantInstance *, bool debug);
	uint64_t download_dependencies(const TenantInstance* ten);
	/* Wait for Varnish to listen and this program to complete initialization. */
	void try_wait_for_startup_and_initialization();
	void unlock_and_initialized(bool success) {
		this->m_initialization_complete = success ? 1 : -1;
		this->mtx_future_init.unlock();
	}

	std::future<long> m_future;
	std::future<long> m_async_start_future;
	std::mutex mtx_future_init;
	int8_t m_initialization_complete = 0;
	bool m_binary_was_local = false;
	bool m_binary_was_cached = false;
	// EpollServer is to allow WebSockets and other non-HTTP protocols
	// to be used within the program.
	std::deque<EpollServer> m_epoll_systems;
	// WebSocketServer is to allow WebSocket protocol specifically.
	std::unique_ptr<WebSocketServer> m_websocket_systems;
};

inline bool ProgramInstance::wait_for_main_vm()
{
	/* Fast-path after initialization. */
	if (this->m_initialization_complete)
		return this->m_initialization_complete > 0;

	std::scoped_lock lock(this->mtx_future_init);

	return this->m_initialization_complete > 0;
}

} // kvm
