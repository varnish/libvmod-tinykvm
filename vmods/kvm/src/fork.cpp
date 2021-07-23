#include "tenant_instance.hpp"
#include "varnish.hpp"

inline MachineInstance* get_machine(VRT_CTX, const void* key)
{
	auto* priv_task = VRT_priv_task(ctx, key);
	//printf("priv_task: ctx=%p bo=%p key=%p task=%p\n", ctx, ctx->bo, key, priv_task);
	if (priv_task->priv && priv_task->len == KVM_PROGRAM_MAGIC)
		return (MachineInstance*) priv_task->priv;
	return nullptr;
}
inline MachineInstance* get_machine(VRT_CTX)
{
	return get_machine(ctx, ctx);
}

MachineInstance* TenantInstance::vmfork(const vrt_ctx*, bool debug)
{
	return nullptr;
}
