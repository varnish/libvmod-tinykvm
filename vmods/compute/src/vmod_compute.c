/**
 * @file vmod_compute.c
 * 
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-09-09
 * 
 * This file is the entry point for all compute VMOD interaction with VCL.
 * 
 */
#include "vmod_compute.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_if.h"

static const int NO_INIT_PROGRAMS = 0;

VCL_BOOL vmod_library(VRT_CTX, VCL_PRIV task, VCL_STRING uri)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: library() should only be called from vcl_init");
		return (0);
	}

	return (kvm_init_tenants_uri(ctx, task, uri, NO_INIT_PROGRAMS));
}

VCL_BOOL vmod_configure(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: configure() should only be called from vcl_init");
		return (0);
	}

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx, "No configuration provided for '%s'", program);
		return (0);
	}

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant != NULL) {
		return (kvm_tenant_configure(ctx, tenant, json));
	} else {
		VRT_fail(ctx, "No such program '%s' for configure", program);
		return (0);
	}
}

VCL_BOOL vmod_start(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_BOOL async)
{
	(void) async;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: start() should only be called from vcl_init");
		return (0);
	}

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant != NULL) {
		/* TODO: If async == false use kvm_reserve_vm() to synchronize init. */
		return (kvm_tenant_async_start(ctx, tenant));
	} else {
		VRT_fail(ctx, "No such program '%s' for async start", program);
		return (0);
	}
}

extern struct director *vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg);

/* Create a response from a KVM backend. */
VCL_BACKEND vmod_program(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING arg, VCL_STRING json_config)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx, "compute: program() should only be called from vcl_backend_fetch");
		return (NULL);
	}

	return (vmod_vm_backend(ctx, task, program, arg, json_config));
}
