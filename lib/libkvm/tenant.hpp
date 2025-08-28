#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "settings.hpp"
#include <tinykvm/common.hpp>
namespace tinykvm {
	struct vCPU;
}

namespace kvm {

struct TenantGroup {
	std::string name;
	float    max_boot_time; /* Seconds */
	float    max_req_time; /* Seconds */
	float    max_storage_time; /* Seconds */
	uint32_t max_queue_time; /* Seconds */
	uint64_t max_address_space; /* Megabytes */
	uint64_t max_main_memory; /* Megabytes */
	uint32_t max_req_mem; /* Megabytes */
	uint32_t limit_req_mem; /* Megabytes of memory banks to keep after request completion */
	uint32_t shared_memory; /* Megabytes */
	uint64_t hugepage_arena_size = 0; /* Megabytes */
	uint64_t hugepage_requests_arena = 0; /* Megabytes */
	size_t   max_concurrency = 2; /* Request VMs */
	size_t   max_smp         = 0; /* Multi-processing per VM */
	size_t   max_regex    = 64;
	bool     has_storage  = false;
	bool     hugepages    = false;
	bool     split_hugepages = true;
	bool     transparent_hugepages = false;
	bool     allow_debug = false;
	bool     remote_debug_on_exception = false;
	bool     mmap_backed_files = false;
	bool     relocate_fixed_mmap = true;
	bool     control_ephemeral = false;
	bool     ephemeral = true;
	bool     ephemeral_keep_working_memory = true;
	bool     print_stdout = false; /* Print directly to stdout */
	bool     verbose = false;
	bool     verbose_syscalls = false;
	bool     verbose_pagetable = false;
	/* Warmup the VM before starting 'real' request handling. */
	struct Warmup {
		uint16_t num_requests = 0;
		std::string url = "/";
		std::string method = "GET";
		std::unordered_set<std::string> headers {
			"User-Agent: tinykvm"
		};
	};
	std::shared_ptr<Warmup> warmup = nullptr;
	/* When port is non-zero, start an epoll server to receive non-HTTP
	   requests, which is forwarded to the current program. */
	uint16_t server_port = 0;
	uint16_t epoll_systems = 0;
	std::string server_address;
	/* WebSocket systems explicitly use the WebSocket protocol, but otherwise
	   are similar to the epoll systems. */
	uint16_t ws_server_port = 0;
	uint16_t websocket_systems = 0;
	std::string ws_server_address;

	std::vector<std::string> environ {
		"LC_TYPE=C", "LC_ALL=C", "USER=root"
	};

	std::shared_ptr<std::vector<std::string>> main_arguments = nullptr;

	std::vector<tinykvm::VirtualRemapping> vmem_remappings;
	bool vmem_heap_executable = false;

	struct VirtualPath {
		std::string real_path;
		std::string virtual_path; /* Path inside the VM, optional */
		bool writable = false;
		bool symlink = false; /* Treated as a symlink path, to be resolved */
		bool usable_in_fork = false;
		bool prefix = false;
	};
	std::vector<VirtualPath> allowed_paths;
	std::string current_working_directory = "/";

	struct DownloadItem {
		std::string uri;
		std::string filepath;
	};
	std::vector<DownloadItem> downloads;

	void set_max_address(uint64_t newmax_mb) { this->max_address_space = newmax_mb * 1048576ul; }
	void set_max_memory(uint64_t newmax_mb) {
		this->max_main_memory = newmax_mb * 1048576ul;
		// The address space should be at least as large as the memory.
		this->max_address_space = std::max(this->max_address_space, this->max_main_memory);
	}
	void set_max_workmem(uint64_t newmax_mb) { this->max_req_mem = newmax_mb * 1048576ul; }
	void set_limit_workmem_after_req(uint64_t newmax_mb) { this->limit_req_mem = newmax_mb * 1048576ul; }
	void set_shared_mem(uint64_t newmax_mb) { this->shared_memory = newmax_mb * 1048576ul; }
	bool has_epoll_system() const noexcept {
		return (this->server_port != 0 || !this->server_address.empty()) &&
		       this->epoll_systems > 0;
	}
	bool has_websocket_system() const noexcept {
		return (this->ws_server_port != 0 || !this->ws_server_address.empty()) &&
		       this->websocket_systems > 0;
	}

	/* Check that each value has meaning and is not impossibly high or low.
	   Throws an exception with a message explaining the problem. */
	void validation() const;

	TenantGroup(std::string n)
		: name{n}, // From settings.hpp:
		  max_boot_time(STARTUP_TIMEOUT),
		  max_req_time(REQUEST_VM_TIMEOUT),
		  max_storage_time(STORAGE_TIMEOUT),
		  max_queue_time(RESV_QUEUE_TIMEOUT),
		  max_address_space(MAIN_MEMORY_SIZE * 1048576ul),
		  max_main_memory(MAIN_MEMORY_SIZE * 1048576ul),
		  max_req_mem(REQUEST_MEMORY_SIZE * 1048576ul),
		  limit_req_mem(~0U),
		  shared_memory(SHARED_MEMORY_SIZE)
		{}
};

struct TenantConfig
{
	std::string    name;
	uint32_t       hash;
	mutable TenantGroup group;
	std::string    filename;  /* Stored locally here (program path prefix). */
	std::string    key;
	std::string    uri;       /* Can be fetched here (program URI archive). */

	std::string request_program_filename() const noexcept { return this->filename; }
	std::string storage_program_filename() const noexcept { return this->filename + "_storage"; }

	float    max_boot_time() const noexcept { return group.max_boot_time; }
	float    max_req_time(bool debug) const noexcept {
		if (debug) return DEBUG_TIMEOUT;
		return group.max_req_time;
	}
	float    max_storage_time() const noexcept { return group.max_storage_time; }
	uint64_t max_address() const noexcept { return group.max_address_space; }
	uint64_t max_main_memory() const noexcept { return group.max_main_memory; }
	uint32_t max_req_memory() const noexcept { return group.max_req_mem; }
	uint32_t limit_req_memory() const noexcept { return group.limit_req_mem; }
	uint32_t shared_memory() const noexcept { return group.shared_memory; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	bool     print_stdout() const noexcept { return group.print_stdout; }
	bool     has_storage() const noexcept { return group.has_storage; }
	bool     hugepages() const noexcept { return group.hugepages; }
	bool     request_hugepages() const noexcept { return group.hugepage_requests_arena != 0; }
	bool     allow_debug() const noexcept { return group.allow_debug; }
	size_t   max_smp() const noexcept { return group.max_smp; }
	bool     control_ephemeral() const noexcept { return group.control_ephemeral; }
	auto&    environ() const noexcept { return group.environ; }

	TenantConfig(std::string nm, std::string fname, std::string key,
		TenantGroup grp, std::string uri);
	~TenantConfig();

	/* One allowed file for persistence / state-keeping */
	std::string allowed_file;
	/* The filename the guest will use to access the allowed file. */
	static const std::string guest_state_file;
};

}
