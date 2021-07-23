#include "tenant_instance.hpp"
#include "varnish.hpp"
using namespace kvm;

MachineInstance* get_machine(VRT_CTX, const void* key)
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


extern "C"
uint64_t kvm_resolve_name(MachineInstance* inst, const char* func)
{
	/* The tenant structure has lookup caching */
	return inst->tenant().lookup(func);
}

extern "C"
MachineInstance* kvm_fork_machine(const vrt_ctx *ctx, const char *tenant, bool debug)
{
	extern TenantInstance* kvm_tenant_find(VRT_CTX, const char *);
	auto* ten = kvm_tenant_find(ctx, tenant);
	if (UNLIKELY(ten == nullptr))
		return nullptr;

	auto* machine = ten->vmfork(ctx, debug);
	if (UNLIKELY(machine == nullptr))
		return nullptr;

	return machine;
}
