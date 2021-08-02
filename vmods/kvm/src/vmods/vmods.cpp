#include "../machine_instance.hpp"

namespace kvm {
extern void initialize_vmod_goto(const vrt_ctx* ctx);

extern "C"
void initialize_vmods(const vrt_ctx* ctx)
{
	initialize_vmod_goto(ctx);
}

}
