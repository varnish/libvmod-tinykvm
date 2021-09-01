#include "../tenant_instance.hpp"
typedef struct vmod_priv * VCL_PRIV;
struct vrt_ctx;

namespace kvm {
extern void initialize_vmod_goto(const vrt_ctx*, VCL_PRIV);
extern void initialize_vmod_http(const vrt_ctx*, VCL_PRIV);

extern "C"
void initialize_vmods(const vrt_ctx* ctx, VCL_PRIV task)
{
	if (!TenantConfig::begin_dyncall_initialization(task))
		return;

	initialize_vmod_goto(ctx, task);
	initialize_vmod_http(ctx, task);
}

}
