#include "vmod_kvm.h"

# include <vsb.h>
# include <vcl.h>
# include "vcc_if.h"

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

VCL_INT vmod_vm_call(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING func, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	TEN_PTR tenptr = kvm_tenant_find(task, tenant);
	if (tenptr == NULL) {
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (-1);
	}

	KVM_PTR machine = kvm_fork_machine(ctx, tenptr, KVM_FORK_MAIN);
	if (machine == NULL) {
		VRT_fail(ctx, "Unable to fork tenant machine: %s", tenant);
		return (-1);
	}

	return (kvm_call(ctx, machine, func, arg));
}

VCL_INT vmod_vm_callv(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_ENUM func, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	int index = -1;
	if (func == vmod_enum_ON_REQUEST)
		index = 0;
	else {
		VRT_fail(ctx, "Wrong enum: %s", func);
		return (-1);
	}

	TEN_PTR tenptr = kvm_tenant_find(task, tenant);
	if (tenptr == NULL) {
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (-1);
	}

	KVM_PTR machine = kvm_fork_machine(ctx, tenptr, KVM_FORK_MAIN);
	if (machine == NULL) {
		VRT_fail(ctx, "Unable to fork tenant machine: %s", tenant);
		return (-1);
	}

	return (kvm_callv(ctx, machine, index, arg));
}

VCL_INT vmod_vm_synth(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (ctx->method == VCL_MET_SYNTH ||
		ctx->method == VCL_MET_BACKEND_ERROR)
	{
		KVM_PTR machine = kvm_get_machine(ctx);
		if (machine == NULL) {
			VRT_fail(ctx, "vmod_kvm: No active machine");
			return (-1);
		}

		struct vsb *vsb = (struct vsb*) ctx->specific;
		assert(vsb != NULL);

		struct vmod_kvm_synth synth = {
			.vsb = vsb,
			.status = 0,
			.ct_len = 0
		};
		if (kvm_synth(ctx, machine, &synth) < 0)
			return (-1);

		struct http *hp = ctx->http_resp;
		assert(hp != NULL);
		http_PrintfHeader(hp, "Content-Length: %zd", VSB_len(vsb));
		http_PrintfHeader(hp, "Content-Type: %.*s", synth.ct_len, synth.ct_buf);

		return (VSB_len(vsb));
	}
	VRT_fail(ctx, "Wrong VCL function for synth responses");
	return (-1);
}
