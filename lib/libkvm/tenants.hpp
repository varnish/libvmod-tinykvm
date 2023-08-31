#pragma once
#include "tenant_instance.hpp"
#include <unordered_map>
typedef struct vmod_priv * VCL_PRIV;

namespace kvm {
class TenantInstance;

struct Tenants {
	std::unordered_map<uint32_t, TenantInstance> tenants;
};

extern Tenants& tenancy(VCL_PRIV task);

}
