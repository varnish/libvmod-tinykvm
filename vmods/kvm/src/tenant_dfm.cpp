#include "tenants.hpp"

#include "tenant_instance.hpp"
#include "utils/crc32.hpp"
#include "varnish.hpp"
#include <stdexcept>
#define KVM_DYNFUNC_MAGIC  0xa40573f9

namespace kvm {

TenantConfig::dynfun_map& dynamic_functions(VCL_PRIV task)
{
	return tenancy(task).dynamic_functions;
}

void TenantConfig::set_dynamic_call(VCL_PRIV task,
	const std::string& name, ghandler_t handler)
{
	auto& map = dynamic_functions(task);
	const uint32_t hash = crc32(name.c_str(), name.size());
	printf("*** DynCall %s is registered as 0x%X\n", name.c_str(), hash);
	auto it = map.find(hash);
	if (it != map.end()) {
		throw std::runtime_error("set_dynamic_call: Hash collision for " + name);
	}
	map.emplace(hash, std::move(handler));
}
void TenantConfig::reset_dynamic_call(VCL_PRIV task,
	const std::string& name, ghandler_t handler)
{
	auto& map = dynamic_functions(task);
	const uint32_t hash = crc32(name.c_str(), name.size());
	map.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(task, name, std::move(handler));
	}
}
void TenantConfig::set_dynamic_calls(VCL_PRIV task, std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(task, pair.first, std::move(pair.second));
	}
}

} // kvm
