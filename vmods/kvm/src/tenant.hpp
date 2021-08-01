#include <cstdint>
#include <map>
#include <string>

namespace kvm {

struct TenantVMOD {
	std::string name;
	bool        access;
};

struct TenantGroup {
	using vmods_t = std::map<uint32_t, TenantVMOD>;

	std::string name;
	uint64_t max_time; /* milliseconds */
	uint64_t max_memory;
	size_t   max_machines = 64;
	size_t   max_fd       = 512;
	size_t   max_backends = 8;
	size_t   max_regex    = 32;

	vmods_t vmods;

	TenantGroup(std::string n, uint64_t mi, uint64_t mm, vmods_t&& vm = vmods_t{})
		: name{n}, max_time(mi), max_memory(mm),
		  vmods{std::move(vm)}  {}
};

struct TenantConfig
{
	std::string    name;
	std::string    filename;
	TenantGroup    group;

	uint64_t max_time() const noexcept { return group.max_time; }
	uint64_t max_memory() const noexcept { return group.max_memory; }
	uint64_t max_machines() const noexcept { return group.max_machines; }
	size_t   max_fd() const noexcept { return group.max_fd; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	size_t   max_backends() const noexcept { return group.max_backends; }

	TenantConfig(std::string n, std::string f, TenantGroup g)
		: name(n), filename(f), group{std::move(g)} {}
};

}
