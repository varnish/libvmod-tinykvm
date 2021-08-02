#pragma once
#include "tenant.hpp"
#include <unordered_map>
typedef struct vmod_priv * VCL_PRIV;

namespace kvm {
class TenantInstance;

struct Tenants {
	using MapType = std::unordered_map<std::string, TenantInstance*>;
	MapType tenants;
	MapType temporaries;

	TenantConfig::dynfun_map dynamic_functions;
};

extern Tenants& tenancy(VCL_PRIV task);

}
