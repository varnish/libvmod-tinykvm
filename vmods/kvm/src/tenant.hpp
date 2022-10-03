#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "settings.hpp"
typedef struct vmod_priv * VCL_PRIV;
namespace tinykvm { struct vCPU; }

namespace kvm {
class MachineInstance;

struct TenantGroup {
	std::string name;
	float    max_boot_time; /* Seconds */
	float    max_req_time; /* Seconds */
	float    max_storage_time; /* Seconds */
	uint64_t max_main_memory; /* Megabytes */
	uint32_t max_req_mem; /* Megabytes */
	uint32_t limit_req_mem; /* Megabytes of memory banks to keep after request completion */
	uint32_t shared_memory; /* Megabytes */
	size_t   max_concurrency = 4;
	size_t   max_fd       = 32;
	size_t   max_backends = 8;
	size_t   max_regex    = 32;
	bool     hugepages    = false;
	bool     ephemeral_hugepages = false;
	bool     allow_debug = false;
	bool     allow_make_ephemeral = false;

	std::vector<std::string> environ {
		"LC_TYPE=C", "LC_ALL=C", "USER=root"
	};

	std::vector<std::string> allowed_paths;

	void set_max_memory(uint64_t newmax_mb) { this->max_main_memory = newmax_mb * 1048576ul; }
	void set_max_workmem(uint64_t newmax_mb) { this->max_req_mem = newmax_mb * 1048576ul; }
	void set_limit_workmem_after_req(uint64_t newmax_mb) { this->limit_req_mem = newmax_mb * 1048576ul; }
	void set_shared_mem(uint64_t newmax_mb) { this->shared_memory = newmax_mb * 1048576ul; }

	TenantGroup(std::string n)
		: name{n}, // From settings.hpp:
		  max_boot_time(STARTUP_TIMEOUT),
		  max_req_time(REQUEST_VM_TIMEOUT),
		  max_storage_time(STORAGE_TIMEOUT),
		  max_main_memory(MAIN_MEMORY_SIZE * 1048576ul),
		  max_req_mem(REQUEST_MEMORY_SIZE * 1048576ul),
		  shared_memory(SHARED_MEMORY_SIZE)
		{}
};

struct TenantConfig
{
	using ghandler_t = std::function<void(MachineInstance&, tinykvm::vCPU&)>;
	using dynfun_map = std::map<uint32_t, ghandler_t>;

	std::string    name;
	std::string    filename;  /* Stored locally here. */
	std::string    key;
	uint32_t       hash;
	TenantGroup    group;
	std::string    uri;       /* Can be fetched here. */

	float    max_boot_time() const noexcept { return group.max_boot_time; }
	float    max_req_time(bool debug) const noexcept {
		if (debug) return DEBUG_TIMEOUT;
		return group.max_req_time;
	}
	float    max_storage_time() const noexcept { return group.max_storage_time; }
	uint64_t max_main_memory() const noexcept { return group.max_main_memory; }
	uint32_t max_req_memory() const noexcept { return group.max_req_mem; }
	uint32_t limit_req_memory() const noexcept { return group.limit_req_mem; }
	uint32_t shared_memory() const noexcept { return group.shared_memory; }
	size_t   max_fd() const noexcept { return group.max_fd; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	size_t   max_backends() const noexcept { return group.max_backends; }
	bool     hugepages() const noexcept { return group.hugepages; }
	bool     ephemeral_hugepages() const noexcept { return group.ephemeral_hugepages; }
	bool     allow_debug() const noexcept { return group.allow_debug; }
	bool     allow_make_ephemeral() const noexcept { return group.allow_make_ephemeral; }
	bool     allow_verbose_curl() const noexcept { return true; }
	auto&    environ() const noexcept { return group.environ; }

	static bool begin_dyncall_initialization(VCL_PRIV);
	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	static void set_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t);
	static void set_dynamic_calls(VCL_PRIV, std::vector<std::pair<std::string, ghandler_t>>);
	static void reset_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t = nullptr);

	TenantConfig(std::string nm, std::string fname, std::string key,
		TenantGroup grp, std::string uri, dynfun_map& dfm);
	~TenantConfig();

	/* One allowed file for persistence / state-keeping */
	std::string allowed_file;
	/* The filename the guest will use to access the allowed file. */
	static const std::string guest_state_file;

	/* Hash map of string hashes associated with dyncall handlers */
	dynfun_map& dynamic_functions_ref;
};

}
