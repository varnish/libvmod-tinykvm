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
#include <errno.h>
#include <stdio.h>
#define SEMPTY(s)			!((s) && *(s))

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

VCL_BOOL vmod_init_self_requests(VRT_CTX, VCL_PRIV task,
	VCL_STRING unix_path, VCL_STRING uri, VCL_INT max_concurrency)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: self_request() should only be called from vcl_init");
		return (0);
	}

	if (SEMPTY(unix_path)) {
		VRT_fail(ctx, "compute: self_request() Unix path cannot be empty");
		return (0);
	}

	if (SEMPTY(uri)) {
		VRT_fail(ctx, "compute: self_request() URI cannot be empty");
		return (0);
	}

	if (max_concurrency < 1) {
		VRT_fail(ctx, "compute: self_request() max concurrency cannot be < 1");
		return (0);
	}

	return (kvm_set_self_request(ctx, task, unix_path, uri, max_concurrency));
}

VCL_BOOL vmod_invalidate_program(VRT_CTX, VCL_PRIV task, VCL_STRING program)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant != NULL) {
		return (kvm_tenant_unload(ctx, tenant));
	} else {
		VRT_fail(ctx, "No such program '%s' for configure", program);
		return (0);
	}
}

VCL_BOOL vmod_add_program(VRT_CTX, VCL_PRIV task,
	VCL_STRING name, VCL_STRING uri)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: add_program() should only be called from vcl_init");
		return (0);
	}

	char json[1024];
	const int json_len = snprintf(json, sizeof(json),
		"{\"%s\": {\"group\": \"%s\", \"uri\": \"%s\"}}",
		name, "test", uri);

	return (kvm_init_tenants_str(ctx, task, "VCL::add_program()", json, json_len, NO_INIT_PROGRAMS));
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
extern const char *kvm_vm_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg, VCL_STRING on_error);
extern VCL_BOOL kvm_vm_begin_epoll(VRT_CTX, VCL_PRIV, VCL_STRING program,
	int fd, const char *arg);

/* Create a response through a KVM backend. */
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

/* Create a string response from given program. */
VCL_STRING vmod_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING argument, VCL_STRING on_error)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH && ctx->method != VCL_MET_BACKEND_RESPONSE) {
		VRT_fail(ctx, "compute: to_string() should only be called from vcl_backend_fetch or vcl_backend_response");
		return (NULL);
	}

	return (kvm_vm_to_string(ctx, task, program, url, argument, on_error));
}

/* Steal the current clients fd and pass it to the given program. */
VCL_BOOL vmod_steal(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_STRING argument)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_RECV) {
		VRT_fail(ctx, "compute: steal() should only be called from vcl_recv");
		return (0);
	}

	if (argument == NULL)
		argument = "";

	if (ctx->req->http0->protover > 11) {
		VRT_fail(ctx, "compute: steal() only works with HTTP 1.x");
		return (0);
	}

	const int fd = ctx->req->sp->fd;
	if (fd > 0) {
		const int gucci = kvm_vm_begin_epoll(ctx, task, program,
			fd, argument);

		/* Only unset if successful. */
		if (gucci) {
			ctx->req->sp->fd = -1;
			//VRT_handling(ctx, VCL_RET_SYNTH);
		}
		return (gucci);
	}
	/* dup() failed. */
	VSLb(ctx->vsl, SLT_Error,
		 "%s: FD steal failed (%s)",
		 program, strerror(errno));
	return (0);
}
