/**
 * @file program_instance.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Wrapper for a live instance of a KVM program.
 * @version 0.1
 * @date 2022-07-23
 *
 * A program instance wraps all the features of a single program
 * into one structure that can be hot-swapped at run-time. The
 * features include the program, the thread pool used to communicate
 * with the program. The binary and main VM (used as storage), the
 * TP for communicating with main VM. A timer system for handling
 * async tasks (into storage), and stuff needed to live-debug the
 * program.
 * 
 * There is also a separate vCPU (with TP) for fully asynchronous
 * tasks that can use storage at the same time as requests. It is
 * recommended to use the asynchronous vCPU only for safe things
 * like fetching from Varnish in order to produce content.
 * 
**/
#include "program_instance.hpp"

#include "curl_fetch.hpp"
#include "settings.hpp"
#include "tenant_instance.hpp"
#include "scoped_duration.hpp"
#include "timing.hpp"
#include "varnish.hpp"
#include <cstring>
#include <tinykvm/rsp_client.hpp>
#include <sched.h>
#include <unistd.h>
extern "C" {
#include "vtim.h"
extern int usleep(uint32_t usec);
extern int numa_max_node();
}
namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);
extern bool file_writer(const std::string& file, const std::vector<uint8_t>&);
extern void extract_programs_to(kvm::ProgramInstance&, const char *, size_t);
static constexpr bool VERBOSE_STORAGE_TASK = false;
static constexpr bool VERBOSE_PROGRAM_STARTUP = false;

VMPoolItem::VMPoolItem(unsigned reqid, const MachineInstance& main_vm,
	TenantInstance* ten, ProgramInstance* prog)
	: mi {nullptr},
	  tp {REQUEST_VM_NICE, false}
{
	// Spawn forked VM on dedicated thread, blocking.
	// XXX: We are deliberately not catching exceptions here.
	this->task_future = tp.enqueue(
	[=, &main_vm] () -> long {
		this->mi = std::make_unique<MachineInstance> (
			reqid, main_vm, ten, prog);
		return 0;
	});
}

Storage::Storage(std::vector<uint8_t> storage_elf)
	: storage_binary{std::move(storage_elf)}
{
}

ProgramInstance::ProgramInstance(
	std::vector<uint8_t> request_elf,
	std::vector<uint8_t> storage_elf,
	const vrt_ctx* ctx, TenantInstance* ten,
	bool debug)
	: request_binary{std::move(request_elf)},
	  m_storage_queue {STORAGE_VM_NICE, false},
	  rspclient{nullptr}
{
	if (ten->config.has_storage()) {
		m_storage.reset(new Storage(std::move(storage_elf)));
	}
	mtx_future_init.lock();

	this->m_binary_was_local = true;
	this->m_binary_was_cached = false;
	this->m_future = m_storage_queue.enqueue(
	[=] () -> long {
		begin_initialization(ctx, ten, debug);
		return 0;
	});
}
ProgramInstance::ProgramInstance(
	const std::string& uri, std::string ifmodsince,
	const vrt_ctx* ctx, TenantInstance* ten,
	bool debug)
	: request_binary{},
	  m_storage_queue {STORAGE_VM_NICE, false},
	  rspclient{nullptr}
{
	if (ten->config.has_storage()) {
		m_storage.reset(new Storage({}));
	}
	mtx_future_init.lock();

	this->m_binary_was_local = false;
	this->m_future = m_storage_queue.enqueue(
	[=] () -> long {
		try {
			/* Avoid invalid URI, we need to check protocols */
			if (UNLIKELY(uri.size() < 5)) {
				throw std::runtime_error("Invalid URI (too short)");
			}

			/* Helper structure for cURL fetch. */
			struct CurlData {
				TenantInstance*  ten;
				ProgramInstance* prog;
				int status;
				const std::string& uri;
			} data {
				.ten  = ten,
				.prog = this,
				.status = 0,
				.uri = uri
			};

			/* Blocking cURL fetch, retrieving the program. May
			   return 304 if the program is newer locally.
			   TODO: Figure out how trust-worthy stat mtime is.
			   TODO: Verify binaries against hash? */
			int res = kvm_curl_fetch(uri.c_str(),
			[] (void* usr, long status, MemoryStruct* chunk) {
				auto* data = (CurlData *)usr;
				data->status = status;
				if (status == 304) {
					if constexpr (VERBOSE_PROGRAM_STARTUP) {
						printf("Loading '%s' from disk\n", data->ten->config.name.c_str());
					}
					data->prog->request_binary = file_loader(data->ten->config.request_program_filename());
					if (data->prog->has_storage())
					{
						// If the storage binary exists, use it
						if (access(data->ten->config.storage_program_filename().c_str(), R_OK) == 0)
						{
							data->prog->storage().storage_binary =
								file_loader(data->ten->config.storage_program_filename());
						}
						else // Otherwise, use the request program
						{
							data->prog->storage().storage_binary =
								data->prog->request_binary;
						}
					}

				} else if (status == 200) {
					if constexpr (VERBOSE_PROGRAM_STARTUP) {
						printf("Loading '%s' from %s\n",
							data->ten->config.name.c_str(), data->uri.c_str());
					}
					extract_programs_to(*data->prog, chunk->memory, chunk->size);

				} else {
					// Unhandled HTTP status?
					throw std::runtime_error("Fetching program '" + data->ten->config.name + "' failed. URL: " + data->uri);
				}

			}, &data, ifmodsince.c_str());

			if (res != 0) {
				// XXX: Reset binary when it fails.
				this->request_binary = {};
				this->main_vm = nullptr;
				this->m_storage = nullptr;
				this->unlock_and_initialized(false);
				return -1;
			}

			const bool was_file = (__builtin_memcmp(uri.c_str(), "file", 4) == 0);
			this->m_binary_was_cached = (data.status == 304);
			this->m_binary_was_local = this->m_binary_was_cached || was_file;

			/* Initialization phase and request VM forking. */
			begin_initialization(ctx, ten, debug);

			/* Store binary to disk when file was remote and cURL reports 200 OK. */
			if (!this->binary_was_local() && data.status == 200 && !ten->config.filename.empty()) {
				/* Cannot throw, but reports true/false on write success.
					We *DO NOT* care if the write failed. Only a cached binary. */
				file_writer(ten->config.request_program_filename(), this->request_binary);
				/* Also, write storage binary, if it exists. */
				if (has_storage()) {
					if (!this->storage().storage_binary.empty())
						file_writer(ten->config.storage_program_filename(),
							this->storage().storage_binary);
				}
			}

			return 0;
		}
		catch (const std::exception& e) {
			// Report errors here, because we cannot propagate them further.
			// Async initialization does not eventually get() the future.
			VSL(SLT_Error, 0,
				"kvm: Program '%s' failed initialization: %s",
					ten->config.name.c_str(), e.what());
			fprintf(stderr,
				"kvm: Program '%s' failed initialization: %s\n",
					ten->config.name.c_str(), e.what());
			// XXX: Reset binary when it fails.
			this->request_binary = {};
			this->main_vm = nullptr;
			this->m_storage = nullptr;
			this->unlock_and_initialized(false);
			return -1;
		}
	});
}
void ProgramInstance::begin_initialization(const vrt_ctx *ctx, TenantInstance *ten, bool debug)
{
	try {
		const size_t max_vms = ten->config.group.max_concurrency;
		if (max_vms < 1)
			throw std::runtime_error("Concurrency must be at least 1");

		TIMING_LOCATION(t0);

		if (this->has_storage())
		{
			// 1. Create the storage VM, used for shared mutable storage.
			storage().storage_vm = std::make_unique<MachineInstance>
				(storage().storage_binary, ctx, ten, this, true, debug);

			// Run through main, verify wait_for_requests() etc.
			storage().storage_vm->initialize();
			// We do not need a VRT CTX after initialization.
			storage().storage_vm->set_ctx(nullptr);
		}

		// 2. Create the master VM, forked later for request concurrency.
		// NOTE: The request VM can make calls into the storage VM, so
		// we need to initialize storage first!
		main_vm = std::make_unique<MachineInstance>
			(this->request_binary, ctx, ten, this, false, debug);


		if (this->has_storage())
		{
			// Automatic remote connection with storage VM is done by calculating the
			// gigapage of the start address, and if non-zero do a remote connection.
			// Figure out the starting address for storage VM stuff
			auto storage_base_gigapage =
				storage().storage_vm->machine().start_address() >> 30U;
			if (storage_base_gigapage > 0)
			{
				main_vm->machine().remote_connect(storage().storage_vm->machine());
			}
		}

		// Run through main, verify wait_for_requests() etc.
		main_vm->initialize();
		// We do not need a VRT CTX after initialization.
		main_vm->set_ctx(nullptr);

		TIMING_LOCATION(t1);

		// Instantiate forked VMs on dedicated threads, in
		// order to prevent KVM migrations. First we create
		// one forked VM and immediately start accepting
		// requests, while we continously add more concurrency
		// to the queue.

		// Instantiate first forked VM
		// XXX: This can fail and throw an exception,
		// think *long and hard* about the consequences!
		m_vms.emplace_back(0, *main_vm, ten, this);

		// Make sure the first VM is up and running before queueing
		m_vms.front().task_future.get();
		m_vmqueue[0].enqueue(&m_vms.front());

		// Start accepting incoming requests on thread pool.
		this->unlock_and_initialized(true);

		TIMING_LOCATION(t2);

		// Instantiate remaining concurrency
		for (size_t i = 1; i < max_vms; i++) {
			m_vms.emplace_back(i, *main_vm, ten, this);
		}

#ifdef VARNISH_PLUS
		const unsigned n_nodes = std::min(size_t(1) + numa_max_node(), m_vmqueue.size());
#else
		constexpr unsigned n_nodes = 1;
#endif
		size_t initialized = 1;
		// Wait for all the VMs to start running
		for (size_t i = 1; i < m_vms.size(); i++) {
			try {
				auto& vm = m_vms[i];
				vm.task_future.get();
				m_vmqueue[i % n_nodes].enqueue(&vm);
				initialized ++;
			} catch (const std::exception& e) {
				if (ctx->vsl != nullptr)
				VSLb(ctx->vsl, SLT_VCL_Error,
					"%s: Failed to create all request machines, init=%zu",
					ten->config.name.c_str(), initialized);
			}
		}

		(void) t1;
		printf("Program '%s' is loaded (%s, %s, vm=%zu%s, nodes=%d, huge=%d/%d, ready=%.2fms%s)\n",
			main_vm->name().c_str(),
			this->binary_was_local() ? "local" : "remote",
			this->binary_was_cached() ? "cached" : "not cached",
			initialized, ten->config.group.ephemeral ? " ephemeral" : "",
			n_nodes, ten->config.hugepages(), ten->config.request_hugepages(),
			nanodiff(t0, t2) / 1e6,
			debug ? ", remote debug" : "");

	} catch (const std::exception& e) {
		// Make sure we signal that there is no program, if the
		// program fails to intialize.
		main_vm = nullptr;
		m_storage = nullptr;
		this->unlock_and_initialized(false);
		throw;
	}
}
ProgramInstance::~ProgramInstance()
{
	/* Finish starting any request VMs and ignore exceptions. */
	for (size_t i = 1; i < m_vms.size(); i++) {
		auto& vm = m_vms[i];
		if (vm.task_future.valid()) {
			try {
				vm.task_future.get();
			} catch (...) {}
		}
	}

	// NOTE: Thread pools need to wait on jobs here
	m_storage_queue.wait_until_empty();
	m_storage_queue.wait_until_nothing_in_flight();
}

long ProgramInstance::wait_for_initialization()
{
	std::scoped_lock lock(this->mtx_future_init);

	if (!this->m_future.valid()) {
		//fprintf(stderr, "Skipped over %s\n", main_vm->tenant().config.name.c_str());
		return 0;
	}
	auto code = this->m_future.get();

	if (main_vm == nullptr) {
		throw std::runtime_error("The program failed to initialize. Check logs for crash?");
	}

	if (!main_vm->is_waiting_for_requests()) {
		throw std::runtime_error("The main program was not waiting for requests. Did you forget to call 'wait_for_requests()'?");
	}

	return code;
}

uint64_t ProgramInstance::lookup(const char* name) const
{
	return main_vm->resolve_address(name);
}
ProgramInstance::gaddr_t ProgramInstance::entry_at(const int idx) const
{
	return entry_address.at(idx);
}
void ProgramInstance::set_entry_at(const int idx, gaddr_t addr)
{
	entry_address.at(idx) = addr;
}

Reservation ProgramInstance::reserve_vm(const vrt_ctx* ctx,
	TenantInstance* ten, std::shared_ptr<ProgramInstance> prog)
{
	const auto tmo = std::chrono::seconds(ten->config.group.max_queue_time);
	VMPoolItem* slot = nullptr;
	auto t0 = ScopedDuration<CLOCK_MONOTONIC>::nanos_now();
	{
		// Fixate on current NUMA node, for performance reasons.
		int node = numa_node();
		if (UNLIKELY(!m_vmqueue[node].wait_dequeue_timed(slot, tmo))) {
			prog->stats.reservation_timeouts ++; /* Racy, but uncontended */
			throw std::runtime_error("Queue timeout");
		}
		assert(slot && ctx);
	}
	/* Time spent reserving this VM. */
	slot->mi->stats().reservation_time += (ScopedDuration<CLOCK_MONOTONIC>::nanos_now() - t0) * 1e-9;

	/* Set the new active VRT CTX. */
	slot->mi->set_ctx(ctx);

	/* This creates a self-reference, which ensures that open
	   Machine instances will keep the program instance alive. */
	slot->prog_ref = std::move(prog);

	/* What happens when the transaction is done */
	return {slot, vm_free_function};
}
#ifdef VARNISH_PLUS
void ProgramInstance::vm_free_function(void* slotv)
{
#else
void ProgramInstance::vm_free_function(VRT_CTX, void* slotv)
{
	(void)ctx;
#endif
	auto* slot = (VMPoolItem *)slotv;
	auto& mi = *slot->mi;

	// Free regexes, file descriptors etc.
	mi.tail_reset();

	// Reset to the current program (even though it might die before next req).
	mi.reset_to(nullptr, *mi.program().main_vm);

	// XXX: Is this racy? We want to enq the slot with the ref.
	// We are the sole owner of the slot, so no need for atomics here.
	auto ref = std::move(slot->prog_ref);
	// Signal waiters that slot is ready again
	// If there any waiters, they keep the program referenced (atomically)
	ref->m_vmqueue[numa_node()].enqueue(slot);
}

long ProgramInstance::storage_call(tinykvm::Machine& src, gaddr_t func,
	size_t n, VirtBuffer buffers[], gaddr_t res_addr, size_t res_size)
{
	/* Detect wrap-around */
	if (UNLIKELY(res_addr + res_size < res_addr))
		return -1;
	/* Check allow-list for what storage functions are allowed. */
	if (!storage().is_allowed(func))
		throw std::runtime_error("Not allowed to call storage function");

	if constexpr (VERBOSE_STORAGE_TASK) {
		printf("Storage task on main queue\n");
	}
	auto future = m_storage_queue.enqueue(
	[&] () -> long
	{
		if constexpr (VERBOSE_STORAGE_TASK) {
			printf("-> Storage task on main queue ENTERED\n");
		}
		auto& storage_vm = *storage().storage_vm;
		auto& stm = storage_vm.machine();
		uint64_t vaddr = stm.stack_address();
		size_t total_input = 0;

		for (size_t i = 0; i < n; i++) {
			total_input += buffers[i].len;
			vaddr -= buffers[i].len;
			vaddr &= ~(uint64_t)0x7;
			stm.copy_from_machine(vaddr, src, buffers[i].addr, buffers[i].len);
			buffers[i].addr = vaddr;
		}

		vaddr -= n * sizeof(VirtBuffer);
		const uint64_t stm_bufaddr = vaddr;
		stm.copy_to_guest(stm_bufaddr, buffers, n * sizeof(VirtBuffer));

		const uint64_t new_stack = vaddr & ~0xFL;
		storage_vm.stats().input_bytes += total_input;

		try {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("Storage task calling 0x%lX with stack 0x%lX\n",
					func, new_stack);
			}
			const float timeout = storage_vm.tenant().config.max_storage_time();
			storage_vm.begin_call();
			storage_vm.stats().invocations++;
			ScopedDuration cputime(storage_vm.stats().request_cpu_time);

			/* Build call manually. */
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			stm.set_registers(regs);

			/* Check if this is a debug program. */
			if (storage_vm.is_debug()) {
				storage_vm.storage_debugger(timeout);
			} else {
				stm.run(timeout);
			}

			const bool storage_resume   = storage_vm.response_called(2);
			const bool storage_noreturn = storage_vm.response_called(3);

			/* The machine must be stopped, and it must have called storage_[no]return. */
			if (!stm.stopped() || !(storage_resume || storage_noreturn)) {
				throw std::runtime_error("Storage did not respond properly");
			}

			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			if (res_addr != 0x0 && st_res_buffer != 0x0) {
				/* Copy from the storage machine back into tenant VM instance */
				src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
				storage_vm.stats().output_bytes += st_res_size;
			}

			/* If res_addr is zero, we will just return the
			   length provided by storage as-is, to allow some
			   communication without a buffer. */
			const auto retval = (res_addr != 0) ? st_res_size : regs.rsi;

			if (storage_resume)
			{
				/* Resume, run the function to the end, allowing cleanup */
				stm.run(STORAGE_CLEANUP_TIMEOUT);
			}

			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Storage task on main queue returning %llu to 0x%lX\n",
					retval, st_res_buffer);
			}
			return retval;

		} catch (const std::exception& e) {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Storage task on main queue failed: %s\n",
					e.what());
			}
			storage_vm.stats().exceptions++;
			return -1;
		}
	});
	return future.get();
}

long ProgramInstance::storage_task(gaddr_t func, std::string argument)
{
	/* Check allow-list for what storage functions are allowed. */
	if (!storage().is_allowed(func))
		throw std::runtime_error("Not allowed to call storage function");

	std::scoped_lock lock(storage().m_async_mtx);
	// Avoid the last task in case it is still active
	while (storage().m_async_tasks.size() > 1)
		// TODO: Read the return value of the tasks to detect errors
		// XXX: This can re-throw a forwarded exception. Handle it?
		storage().m_async_tasks.pop_front();

	// Block and finish previous async tasks
	storage().m_async_tasks.push_back(
		m_storage_queue.enqueue(
	[=, data_arg = std::move(argument)] () -> long
	{
		if constexpr (VERBOSE_STORAGE_TASK) {
			printf("-> Async task on main queue\n");
		}
		auto& storage_vm = *storage().storage_vm;
		auto& stm = storage_vm.machine();

		try {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("Calling 0x%lX\n", func);
			}
			/* Avoid async storage while still initializing. */
			this->try_wait_for_startup_and_initialization();

			storage_vm.stats().invocations++;
			storage_vm.stats().input_bytes += data_arg.size();
			ScopedDuration cputime(storage_vm.stats().request_cpu_time);

			unsigned long long rsp = stm.stack_address();
			auto data_addr = stm.stack_push(rsp, data_arg);

			stm.timed_vmcall_stack(func, rsp, ASYNC_STORAGE_TIMEOUT,
				uint64_t(data_addr), uint64_t(data_arg.size()));
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Async task finished 0x%lX\n", func);
			}

			return 0;
		} catch (const std::exception& e) {
			if constexpr (VERBOSE_STORAGE_TASK) {
				printf("<- Async task failure: %s\n", e.what());
			}
			storage_vm.stats().exceptions++;
			return -1;
		}
	}));
	return 0;
}

long ProgramInstance::live_update_call(const vrt_ctx* ctx,
	gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc)
{
	struct SerializeResult {
		uint64_t data;
		uint64_t len;
	};
	const float timeout = storage().storage_vm->tenant().config.max_storage_time();

	auto future = m_storage_queue.enqueue(
	[&] () -> long
	{
		try {
			/* Serialize data in the old machine */
			storage().storage_vm->set_ctx(ctx);
			auto& old_machine = storage().storage_vm->machine();
			old_machine.timed_vmcall(func, timeout);
			return 0;
		} catch (...) {
			/* We have to make sure Varnish is not taken down */
			return -1;
		}
	});
	long result = future.get();

	SerializeResult from {};
	if (result == 0) {
		auto& old_machine = storage().storage_vm->machine();
		/* Get serialized data */
		auto regs = old_machine.registers();
		auto data_addr = regs.rdi;
		auto data_len = regs.rsi;
		if (data_addr + data_len < data_addr) {
			return -1;
		}
		from = {data_addr, data_len};
	}
	if (from.data == 0x0)
		return -1;

	auto new_future = new_prog.m_storage_queue.enqueue(
	[&] () -> long
	{
		try {
			auto &new_machine = new_prog.storage().storage_vm->machine();
			/* Begin resume procedure */
			new_prog.storage().storage_vm->set_ctx(ctx);

			new_machine.timed_vmcall(newfunc, timeout, (uint64_t)from.len);

			auto regs = new_machine.registers();
			/* The machine should be calling STOP with rsi=dst_data */
			auto res_data = regs.rdi;
			auto res_size = std::min((uint64_t)regs.rsi, from.len);
			if (res_data != 0x0)
			{ // Just a courtesy, we *do* check permissions.
				auto& old_machine = storage().storage_vm->machine();
				new_machine.copy_from_machine(
					res_data, old_machine, from.data, res_size);
				/* Resume the new machine, allowing it to deserialize data */
				new_machine.run(STORAGE_DESERIALIZE_TIMEOUT);
				return res_size;
			}
			return 0;
		}
		catch (...)
		{
			return -1;
		}
	});

	return new_future.get();
}

static int varnish_is_accepting_connections() {
	static bool waited = false;
	if (UNLIKELY(!waited)) {
		waited = true;
		usleep(250'000); /* Sleep 250ms */
	}
	// TODO: Use VCA_AcceptingConnections()
	return 1;
}

void ProgramInstance::try_wait_for_startup_and_initialization()
{
	/* Avoid async storage while still initializing. 100ms intervals. */
	static const int MAX_RETRIES = 50;
	static const uint32_t WAIT_TIME = 100'000; /* 100ms */

	int retries = MAX_RETRIES; /* 5000ms maximum wait */
	while (
		(m_initialization_complete == 0 || !varnish_is_accepting_connections())
		&& retries > 0)
	{
		usleep(WAIT_TIME);
		retries--;
	}
	if constexpr (VERBOSE_STORAGE_TASK) {
		printf("Storage: Waited %d times...\n", MAX_RETRIES - retries);
	}
}

int ProgramInstance::numa_node()
{
#ifdef __x86_64__
	unsigned long a,d,c;
	__asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
	return c >> 12;
#else
	unsigned cpu_id, node_id;
	return (getcpu(&cpu_id, &node_id) ? 0 : node_id);
#endif
}

} // kvm
