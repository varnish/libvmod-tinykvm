#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
typedef struct vmod_priv * VCL_PRIV;

namespace kvm {
class MachineInstance;

struct TenantVMOD {
	std::string name;
	bool        access;
};

struct TenantGroup {
	using vmods_t = std::map<uint32_t, TenantVMOD>;

	std::string name;
	uint64_t max_time; /* milliseconds */
	uint64_t max_memory;
	uint32_t max_work_mem;
	size_t   max_machines = 64;
	size_t   max_fd       = 32;
	size_t   max_backends = 8;
	size_t   max_regex    = 32;

	std::vector<std::string> allowed_paths {
		"/usr/lib/locale",
		"/usr/share/locale",
		"/usr/local/share/espeak-ng-data",
		"/usr/share/mbrola/en1",
	};

	vmods_t vmods;

	TenantGroup(std::string n, uint64_t mi, uint64_t mm, uint64_t mwm, vmods_t&& vm = vmods_t{})
		: name{n}, max_time(mi), max_memory(mm), max_work_mem(mwm),
		  vmods{std::move(vm)}  {}
};

struct TenantConfig
{
	using ghandler_t = std::function<void(MachineInstance&)>;
	using dynfun_map = std::map<uint32_t, ghandler_t>;

	std::string    name;
	std::string    filename;
	std::string    key;
	TenantGroup    group;

	uint64_t max_time() const noexcept { return group.max_time; }
	uint64_t max_memory() const noexcept { return group.max_memory; }
	uint32_t max_work_memory() const noexcept { return group.max_work_mem; }
	uint64_t max_machines() const noexcept { return group.max_machines; }
	size_t   max_fd() const noexcept { return group.max_fd; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	size_t   max_backends() const noexcept { return group.max_backends; }

	// Install a callback function using a string name
	// Can be invoked from the guest using the same string name
	static void set_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t);
	static void set_dynamic_calls(VCL_PRIV, std::vector<std::pair<std::string, ghandler_t>>);
	static void reset_dynamic_call(VCL_PRIV, const std::string& name, ghandler_t = nullptr);

	TenantConfig(std::string n, std::string f, std::string k, TenantGroup g, dynfun_map& dfm)
		: name(n), filename(f), key(k), group{std::move(g)}, dynamic_functions_ref{dfm} {}

	/* Hash map of string hashes associated with dyncall handlers */
	dynfun_map& dynamic_functions_ref;
};

}
