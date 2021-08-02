typedef struct vmod_priv * VCL_PRIV;
struct vrt_ctx;

namespace kvm {
extern void initialize_vmod_goto(const vrt_ctx*, VCL_PRIV);

extern "C"
void initialize_vmods(const vrt_ctx* ctx, VCL_PRIV task)
{
	initialize_vmod_goto(ctx, task);
}

}
