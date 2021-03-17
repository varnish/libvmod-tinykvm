#include <cstdint>
#include <string>
#include <unordered_map>

struct TenantVMOD {
	std::string name;
	bool        access;
};

struct TenantGroup {
	using vmods_t = std::unordered_map<uint32_t, TenantVMOD>;

	std::string name;
	uint64_t max_instructions;
	uint64_t max_memory;
	uint64_t max_heap;
	size_t   max_backends = 8;
	size_t   max_regex    = 32;

	vmods_t vmods;

	TenantGroup(std::string n, uint64_t mi, uint64_t mm, uint64_t mh, vmods_t&& vm = vmods_t{})
		: name{n}, max_instructions(mi), max_memory(mm), max_heap(mh),
		  vmods{std::move(vm)}  {}
};

struct TenantConfig
{
	std::string    name;
	std::string    filename;
	TenantGroup    group;

	uint64_t max_instructions() const noexcept { return group.max_instructions; }
	uint64_t max_memory() const noexcept { return group.max_memory; }
	uint64_t max_heap() const noexcept { return group.max_heap; }
	size_t   max_regex() const noexcept { return group.max_regex; }
	size_t   max_backends() const noexcept { return group.max_backends; }

	TenantConfig(std::string n, std::string f, TenantGroup g)
		: name(n), filename(f), group{std::move(g)} {}
};
