#pragma once
#include "tenant_instance.hpp"
#include <unordered_map>
typedef struct vmod_priv * VCL_PRIV;

namespace kvm {
class TenantInstance;

struct Tenants {
	using MapType = std::unordered_map<uint32_t, TenantInstance>;
	MapType tenants;
	MapType temporaries;

	TenantConfig::dynfun_map dynamic_functions;
	bool dyncalls_initialized = false;
};

extern Tenants& tenancy(VCL_PRIV task);

}
