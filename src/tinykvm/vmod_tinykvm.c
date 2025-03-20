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
#include "vmod_tinykvm.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_tinykvm_if.h"
#ifndef VARNISH_PLUS
#include <vre.h>
#endif
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

VCL_STRING vmod_stats(VRT_CTX, VCL_PRIV task, VCL_STRING pattern, VCL_INT indent)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (pattern == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
			"compute.stats(): Regex pattern was null");
		return ("{}");
	}
	if (indent < -1)
		indent = -1;
	if (indent > 16)
		indent = 16;

	return kvm_json_stats(ctx, task, pattern, (unsigned)indent);
}

struct unloader_state {
	VRT_CTX;
	vre_t *regex;
};
static int unloader(const char *program, struct vmod_kvm_tenant *tenant, void *vstate)
{
	const struct unloader_state *state = (const struct unloader_state *)vstate;

#ifdef VARNISH_PLUS
	const int matches =
		VRE_exec(state->regex, program, strlen(program), 0,
			0, NULL, 0, NULL);
#else
	const int matches =
		VRE_match(state->regex, program, strlen(program), 0, NULL);
#endif
	if (matches > 0) {
		VSLb(state->ctx->vsl, SLT_VCL_Log,
			"compute: Unloading '%s'", program);
		return kvm_tenant_unload(state->ctx, tenant);
	}
	return (0);
}
VCL_INT vmod_invalidate_programs(VRT_CTX, VCL_PRIV task, VCL_STRING pattern)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (pattern == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
			"compute: Regex pattern was null");
		return (0);
	}

	/* Compile regex pattern (NOTE: uses a lot of stack). */
#ifdef VARNISH_PLUS
	const char* error = "";
	int         error_offset = 0;
	vre_t *re = VRE_compile(pattern, 0, &error, &error_offset);
#else
	int error = 0;
	int error_offset = 0;
	vre_t *re = VRE_compile(pattern, 0, &error, &error_offset, 0);
#endif

	if (re == NULL) {
		/* TODO: Nice regex error explanation. */
		(void)error;
		(void)error_offset;
		VSLb(ctx->vsl, SLT_VCL_Error,
			"compute: Regex '%s' compilation failed", pattern);
		return (0);
	}

	/* Unload programs matching the pattern. */
	struct unloader_state state = {
		.ctx = ctx,
		.regex = re,
	};
	int count = kvm_tenant_foreach(task, unloader, &state);
	VSLb(ctx->vsl, SLT_VCL_Log,
		"compute: Unloaded %d programs matching '%s'", count, pattern);

	VRE_free(&re);

	return (count);
}

VCL_BOOL vmod_configure(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "compute: configure() should only be called from vcl_init");
		return (0);
	}

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx,
			"compute: No configuration provided for '%s'", program);
		return (0);
	}

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant != NULL) {
		return (kvm_tenant_configure(ctx, tenant, json));
	} else {
		char pjson[1024];
		const int pjson_len = snprintf(pjson, sizeof(pjson),
			"{\"%s\": %s}",
			program, json);
		return (kvm_init_tenants_str(ctx, task,
			"compute::configure()", pjson, pjson_len, NO_INIT_PROGRAMS));
	}
}

VCL_BOOL vmod_main_arguments(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_STRANDS arguments)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (arguments == NULL || arguments->n == 0) {
		VSLb(ctx->vsl, SLT_VCL_Log,
			"compute: No main arguments provided for '%s'", program);
		return (0);
	}

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx,
			"compute: No such program '%s'", program);
		return (0);
	}

	return (kvm_tenant_arguments(ctx, tenant, arguments->n, arguments->p));
}

VCL_BOOL vmod_start(VRT_CTX, VCL_PRIV task, VCL_STRING program, VCL_BOOL async, VCL_BOOL debug)
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
		return (kvm_tenant_async_start(ctx, tenant, debug));
	} else {
		VRT_fail(ctx,
			"compute: No such program '%s' for async start", program);
		return (0);
	}
}

extern struct director *vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg);
extern const char *kvm_vm_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg, VCL_STRING on_error, VCL_INT);
extern int kvm_vm_synth(VRT_CTX, VCL_PRIV task, VCL_INT status,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg);
extern VCL_BOOL kvm_vm_begin_epoll(VRT_CTX, VCL_PRIV, VCL_STRING program,
	int fd, const char *arg);

/* Create a response through a KVM backend. */
VCL_BACKEND vmod_program(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING arg, VCL_STRING json_config)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx,
			"compute: program() should only be called from vcl_backend_fetch");
		return (NULL);
	}

	return (vmod_vm_backend(ctx, task, program, arg, json_config));
}

/* Create a string response from given program and arguments. */
VCL_STRING vmod_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING argument,
	VCL_STRING on_error, VCL_INT error_treshold)
{
	return (kvm_vm_to_string(ctx, task, program, url, argument, on_error, error_treshold));
}

/* Create a synthetic response from given program and arguments. */
VCL_INT vmod_synth(VRT_CTX, VCL_PRIV task, VCL_INT status,
	VCL_STRING program, VCL_STRING url, VCL_STRING argument)
{
	return (kvm_vm_synth(ctx, task, status, program, url, argument));
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
