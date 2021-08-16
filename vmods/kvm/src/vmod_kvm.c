#include "vmod_kvm.h"

VCL_VOID vmod_embed_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx, "kvm.embed_tenants() requires a JSON string");
		return;
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	kvm_init_tenants_str(ctx, task, json);
}

VCL_VOID vmod_load_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (filename == NULL || filename[0] == 0) {
		VRT_fail(ctx, "kvm.load_tenants() requires a filename");
		return;
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	kvm_init_tenants_file(ctx, task, filename);
}

VCL_BOOL vmod_tenant_is_ready(VRT_CTX, VCL_PRIV task, VCL_STRING tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (tenant == NULL || tenant[0] == 0) {
		VRT_fail(ctx, "kvm.tenant_is_ready() requires a tenant name");
		return (0);
	}

	struct vmod_kvm_tenant *tenptr = kvm_tenant_find(task, tenant);

	if (tenptr == NULL) {
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (0);
	}

	return (kvm_tenant_gucci(tenptr, 0));
}

VCL_VOID vmod_cache_symbol(VRT_CTX, VCL_STRING symbol)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (symbol == NULL || symbol[0] == 0) {
		VRT_fail(ctx, "kvm.cache_symbol() requires a symbol name");
		return;
	}

	kvm_cache_symbol(symbol);
}

VCL_BOOL vmod_fork(VRT_CTX, VCL_PRIV task, VCL_STRING tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_kvm_tenant *tenptr = kvm_tenant_find(task, tenant);

	if (tenptr == NULL) {
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (0);
	}

	return (kvm_fork_machine(ctx, tenptr, 0) != NULL);
}
