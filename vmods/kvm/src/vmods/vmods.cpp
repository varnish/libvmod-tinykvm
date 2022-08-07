#include "../tenant_instance.hpp"
typedef struct vmod_priv * VCL_PRIV;
struct vrt_ctx;

namespace kvm {
extern void initialize_curl(const vrt_ctx*, VCL_PRIV);
extern void initialize_facedetect(const vrt_ctx*, VCL_PRIV);

extern "C"
void initialize_vmods(const vrt_ctx* ctx, VCL_PRIV task)
{
	if (!TenantConfig::begin_dyncall_initialization(task))
		return;

	initialize_curl(ctx, task);
#ifdef KVM_FACEDETECTION
	initialize_facedetect(ctx, task);
#endif
}

}
