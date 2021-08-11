#include "common_defs.hpp"
#include "machine_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
using namespace kvm;

kvm::MachineInstance* kvm_get_machine(VRT_CTX, const void* key)
{
	auto* priv_task = VRT_priv_task(ctx, key);
	//printf("priv_task: ctx=%p bo=%p key=%p task=%p\n", ctx, ctx->bo, key, priv_task);
	if (priv_task->priv && priv_task->len == KVM_PROGRAM_MAGIC)
		return (kvm::MachineInstance*) priv_task->priv;
	return nullptr;
}
inline kvm::MachineInstance* kvm_get_machine(VRT_CTX)
{
	return kvm_get_machine(ctx, ctx);
}


extern "C"
uint64_t kvm_resolve_name(kvm::TenantInstance* tenant, const char* func)
{
	/* The tenant structure has lookup caching */
	return tenant->lookup(func);
}

extern "C"
kvm::MachineInstance* kvm_fork_machine(const vrt_ctx *ctx, kvm::TenantInstance* tenant, bool debug)
{
	return tenant->vmfork(ctx, debug);
}

extern "C"
int kvm_copy_to_machine(kvm::MachineInstance* machine,
	uint64_t dst, const void* src, size_t len)
{
	try {
		machine->copy_to(dst, src, len);
		return 0;
	} catch (...) {
		return -1;
	}
}
