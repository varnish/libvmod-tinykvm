/**
 * @file vmod_kvm.c
 * 
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-07-23
 * 
 * This file is the entry point for all VMOD KVM interaction with VCL.
 * The starting point is often kvm.embed_tenants, which is implemented
 * as vmod_embed_tenants by the VCC generator script.
 * 
 * The second most important function is vm_backend, which is implemented
 * in kvm_backend.c, with the entry point at the bottom.
 * 
 */
#include "vmod_kvm.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_if.h"
#include <malloc.h>

static const int INIT_TENANTS = 1;

VCL_BOOL vmod_tenant_is_ready(VRT_CTX, VCL_PRIV task, VCL_STRING tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (tenant == NULL || tenant[0] == 0)
	{
		VRT_fail(ctx, "kvm.tenant_is_ready() requires a tenant name");
		return (0);
	}

	struct vmod_kvm_tenant *tenptr = kvm_tenant_find(task, tenant);

	if (tenptr == NULL)
	{
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (0);
	}

	return (kvm_tenant_gucci(tenptr, 0));
}

VCL_VOID vmod_embed_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx, "kvm.embed_tenants() requires a JSON string");
		return;
	}

	kvm_init_tenants_str(ctx, task, "Embedded JSON", json, strlen(json), INIT_TENANTS);
}

VCL_BOOL vmod_load_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (filename == NULL || filename[0] == 0) {
		VRT_fail(ctx, "kvm.load_tenants() requires a filename");
		return (0);
	}

	return (kvm_init_tenants_file(ctx, task, filename, INIT_TENANTS));
}

VCL_BOOL vmod_fetch_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING url)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (url == NULL || url[0] == 0) {
		VRT_fail(ctx, "kvm.fetch_tenants() requires a URL");
		return (0);
	}

	return (kvm_init_tenants_uri(ctx, task, url, INIT_TENANTS));
}
