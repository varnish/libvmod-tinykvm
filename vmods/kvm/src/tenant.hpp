#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
typedef struct vmod_priv * VCL_PRIV;
namespace tinykvm { struct vCPU; }

namespace kvm {
class MachineInstance;

struct TenantVMOD {
	std::string name;
	bool        access;
};

struct TenantGroup {
	using vmods_t = std::map<uint32_t, TenantVMOD>;

	std::string name;
	float    max_time; /* Seconds */
	float    max_boot_time; /* Seconds */
	uint64_t max_memory; /* Megabytes */
	uint32_t max_work_mem; /* Megabytes */
	uint32_t shared_memory; /* Megabytes */
	size_t   max_concurrency = 4;
	size_t   max_fd       = 32;
	size_t   max_backends = 8;
	size_t   max_regex    = 32;
	bool     hugepages    = false;
	bool     ephemeral_hugepages = false;
	bool     allow_make_ephemeral = false;

	std::vector<std::string> environ {
		"LC_TYPE=C", "LC_ALL=C", "USER=root"
	};

	std::vector<std::string> allowed_paths;

	vmods_t vmods;

	void set_max_memory(uint64_t newmax_mb) { this->max_memory = newmax_mb * 1048576ul; }
	void set_max_workmem(uint64_t newmax_mb) { this->max_work_mem = newmax_mb * 1048576ul; }
	void set_shared_mem(uint64_t newmax_mb) { this->shared_memory = newmax_mb * 1048576ul; }

	TenantGroup(std::string n, float t, uint64_t mm, uint64_t mwm, vmods_t&& vm = vmods_t{})
		: name{n}, max_time(t), max_boot_time(10.0),
		  max_memory(mm * 1048576ul), max_work_mem(mwm * 1048576ul),
		  shared_memory(max_memory / 8),
		  vmods{std::move(vm)}  {}
};

struct TenantConfig
{
	using ghandler_t = std::function<void(MachineInstance&, tinykvm::vCPU&)>;
	using dynfun_map = std::map<uint32_t, ghandler_t>;

	std::string    name;
	std::string    filename;
	std::string    key;
	uint32_t       hash;
	TenantGroup    group;

	float    max_boot_time() const noexcept { return group.max_boot_time; }
	float    max_time() const noexcept { return group.max_time; }
	uint64_t max_memory() const noexcept { return group.max_memory; }
	uint32_t max_work_memory() const noexcept { return group.max_work_mem; }
	uint32_t shared_memory() const noexcept { return group.shared_memory; }
	size_t   max_fd() const noexcept { return group.max_fd; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	size_t   max_backends() const noexcept { return group.max_backends; }
	bool     hugepages() const noexcept { return group.hugepages; }
	bool     ephemeral_hugepages() const noexcept { return group.ephemeral_hugepages; }
	bool     allow_make_ephemeral() const noexcept { return group.allow_make_ephemeral; }
	auto&    environ() const noexcept { return group.environ; }

	static bool begin_dyncall_initialization(VCL_PRIV);
	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	static void set_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t);
	static void set_dynamic_calls(VCL_PRIV, std::vector<std::pair<std::string, ghandler_t>>);
	static void reset_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t = nullptr);

	TenantConfig(std::string n, std::string f, std::string k,
		TenantGroup g, dynfun_map& dfm);
	~TenantConfig();

	/* One allowed file for persistence / state-keeping */
	std::string allowed_file;
	/* The filename the guest will use to access the allowed file. */
	static const std::string guest_state_file;

	/* Hash map of string hashes associated with dyncall handlers */
	dynfun_map& dynamic_functions_ref;
};

}
