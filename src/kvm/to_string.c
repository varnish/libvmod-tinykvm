/**
 * @file to_string.c
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2023-08-10
 *
 * to_string() produces a string result after passing
 * data through a chain of VMs.
 * The strings are currently zero-terminated.
 *
 */
#include "../kvm/vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vsb.h>
#include <vtim.h>
#include "vcl.h"

#include "vcc_if.h"
#include "VSC_vmod_kvm.h"

extern void kvm_varnishstat_program_cpu_time(vtim_real);
extern void kvm_backend_call(VRT_CTX, KVM_SLOT,
	const struct kvm_chain_item *, struct backend_post *, struct backend_result *);
extern struct kvm_program_chain* kvm_chain_get_queue();
extern struct kvm_chain_item *kvm_init_chain(VRT_CTX, struct vmod_kvm_tenant *tenant,
	const char *url, const char *arg);
extern int kvm_handle_post_to_another(VRT_CTX, struct backend_post *post,
	struct kvm_chain_item *invocation, struct backend_result *result);

struct kvm_http_response {
	const char* ctype;
	size_t ctype_size;

	const char* content;
	size_t content_size;
	bool   writable_content;

	uint16_t status;
};
static struct kvm_http_response
minimal_response(uint16_t status, const char *content)
{
	AN(content);
	return (struct kvm_http_response) {
		.ctype = "text/plain",
		.ctype_size = strlen("text/plain"),
		.content = content,
		.content_size = strlen(content),
		.writable_content = false,
		.status = status
	};
}

typedef void (*content_func_f)(const struct backend_result *, struct kvm_http_response *, void *);
static struct kvm_http_response
to_string(VRT_CTX, struct kvm_program_chain *chain, content_func_f content_callback, void *content_opaque)
{
	/* Program cpu-time t0. */
	const vtim_real t0 = VTIM_real();

	/* The backend_result contains many iovec-like buffers needed for
	   extracting data from the VM without copying to a temporary buffer. */
	struct backend_result *result =
		(struct backend_result *)WS_Alloc(ctx->ws, VMBE_RESULT_SIZE);
	if (result == NULL) {
		VSLb(ctx->vsl, SLT_Error, "KVM: Out of workspace for result");
		return (minimal_response(500, "Out of workspace"));
	}
	result->bufcount = VMBE_NUM_BUFFERS;

	/* Post struct for chaining */
	struct backend_post *post = NULL;
	if (chain->count > 1) {
		post = (struct backend_post *)WS_Alloc(ctx->ws, sizeof(struct backend_post));
		if (post == NULL) {
			VSLb(ctx->vsl, SLT_Error, "KVM: Out of workspace for POST structure");
			return (minimal_response(500, "Out of workspace"));
		}
		post->ctx = ctx;
	}

	struct vmod_kvm_slot *last_slot = NULL;

	for (int index = 0; index < chain->count; index++)
	{
		/* The chain item contains the tenant program and all the inputs
		   to the program. Everything needed to invoke the VM function. */
		struct kvm_chain_item *invocation =
			&chain->chain[index];

		/* Reserving a VM means putting ourselves in a concurrent queue
		waiting for a free VM, and then getting exclusive access until
		the end of the request. */
		struct vmod_kvm_slot *slot =
			kvm_temporarily_reserve_machine(ctx, invocation->tenant, false);
		if (slot == NULL) {
			/* Global program cpu-time statistic. */
			kvm_varnishstat_program_cpu_time(VTIM_real() - t0);

			if (last_slot != NULL) {
				kvm_free_reserved_machine(ctx, last_slot);
			}
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Unable to reserve '%s'", kvm_tenant_name(invocation->tenant));
			return (minimal_response(500, "Unable to make reservation"));
		}

		bool use_post = false;
		if (index > 0)
		{
			/* Allocate exact bytes from previous result in reserved VM */
			post->slot = slot;
			post->address = 0;
			post->capacity = result->content_length;
			post->length  = 0;
			post->inputs = invocation->inputs;

			if (kvm_handle_post_to_another(ctx, post, invocation, result) < 0)
			{
				/* Global program cpu-time statistic. */
				kvm_varnishstat_program_cpu_time(VTIM_real() - t0);

				kvm_free_reserved_machine(ctx, slot);
				kvm_free_reserved_machine(ctx, last_slot);
				return (minimal_response(500, "Unable to transfer in chain"));
			}

			kvm_free_reserved_machine(ctx, last_slot);

			use_post = true;
		}

		/* Make a backend VM call (with optional POST). */
		kvm_backend_call(ctx, slot, invocation, use_post ? post : NULL, result);

		if (result->status >= invocation->break_status) {
			/* Global program cpu-time statistic. */
			kvm_varnishstat_program_cpu_time(VTIM_real() - t0);

			kvm_free_reserved_machine(ctx, slot);
			if (last_slot != NULL) {
				kvm_free_reserved_machine(ctx, last_slot);
			}
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Error status %u from call to %s",
				result->status, kvm_tenant_name(invocation->tenant));
			return (minimal_response(result->status, "Error status in chain"));
		}

		last_slot = slot;
	}

	/* Global program cpu-time statistic. */
	kvm_varnishstat_program_cpu_time(VTIM_real() - t0);

	struct kvm_http_response resp;
	resp.status     = result->status;
	resp.ctype      = result->type;
	resp.ctype_size = result->tsize;

	/* Finalize result by calling the content callback. */
	content_callback(result, &resp, content_opaque);

	if (last_slot != NULL) {
		kvm_free_reserved_machine(ctx, last_slot);
	}
	return (resp);
}


static void to_string_callback(const struct backend_result *result, struct kvm_http_response *resp, void *opaque)
{
	struct vrt_ctx *ctx = (struct vrt_ctx *)opaque;
	struct ws *ws = ctx->ws;
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	/* Copy the result into the workspace */
	size_t total_size = 0;
	for (size_t i = 0; i < result->bufcount; i++) {
		total_size += result->buffers[i].size;
	}

	/* Allocate content-length + zero-termination */
	char *content = (char *)WS_Alloc(ws, total_size + 1);
	if (content == NULL) {
		VRT_fail(ctx,
			"KVM: Out of workspace for final content (size=%zu bytes)", total_size);
		resp->content = "";
		resp->content_size = 0;
		return;
	}

	/* Copy the content */
	char *coff = content;
	for (size_t i = 0; i < result->bufcount; i++) {
		memcpy(coff, result->buffers[i].data, result->buffers[i].size);
		coff += result->buffers[i].size;
	}
	/* Zero-terminate content */
	content[total_size] = 0;

	/* Set the content */
	resp->content = content;
	resp->content_size = total_size;
	resp->writable_content = true;
}

VCL_STRING kvm_vm_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg, VCL_STRING on_error,
	VCL_INT error_treshold)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Program not found: %s", program);
		__sync_fetch_and_add(&vsc_vmod_kvm->program_notfound, 1);
		return (NULL);
	}

	/* Cannot fail at this point, add to chain */
	struct kvm_chain_item *invocation =
		kvm_init_chain(ctx, tenant, url, arg);
	if (invocation == NULL) {
		return (on_error);
	}

	invocation->inputs.method = "GET"; /* Convenience */
	invocation->inputs.content_type = "";
	invocation->break_status = error_treshold;

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

	struct kvm_http_response resp = to_string(ctx, &chain, &to_string_callback, (void *)ctx);
	if (resp.status < error_treshold)
		return (resp.content);
	else
		return (on_error);
}

static void to_synth_callback(const struct backend_result *result, struct kvm_http_response *resp, void *opaque)
{
	struct vrt_ctx *ctx = (struct vrt_ctx *)opaque;
	struct ws *ws = ctx->ws;
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	/* In vcl_synth or vcl_backend_error, we have access to a VSB for content */
	struct vsb *vsb = (struct vsb *)ctx->specific;
	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);

	/* Copy the content type */
	resp->ctype = result->type;
	resp->ctype_size = result->tsize;

#ifdef VARNISH_PLUS
	const size_t len = result->content_length;
	// Attempted fast-path when the total length is < 16KB
	if (vsb->s_size < len && len < kvm_settings.backend_early_release_size) {
		//printf("KVM: Synth fast-path for small length (size=%zu bytes)\n", len);
		char* buffer = WS_Alloc(ws, len + 1);
		if (buffer != NULL) {
			VSB_delete(vsb);
			VSB_new(vsb, buffer, len + 1, VSB_FIXEDLEN);
			for (size_t i = 0; i < result->bufcount; i++) {
				VSB_bcat(vsb, result->buffers[i].data, result->buffers[i].size);
			}
			return;
		}
	}
#endif
	VSB_clear(vsb);
	for (size_t i = 0; i < result->bufcount; i++) {
		VSB_bcat(vsb, result->buffers[i].data, result->buffers[i].size);
	}
}

VCL_INT kvm_vm_synth(VRT_CTX, VCL_PRIV task, VCL_INT status,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

	if (ctx->method != VCL_MET_SYNTH && ctx->method != VCL_MET_BACKEND_ERROR) {
		VRT_fail(ctx, "KVM: synth() must be called from vcl_synth or vcl_backend_error");
		return (0);
	}

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Program not found: %s", program);
		__sync_fetch_and_add(&vsc_vmod_kvm->program_notfound, 1);
		return (0);
	}

	/* Cannot fail at this point, add to chain */
	struct kvm_chain_item *invocation =
		kvm_init_chain(ctx, tenant, url, arg);
	if (invocation == NULL) {
		return (0);
	}

	invocation->inputs.method = "GET"; /* Convenience */
	invocation->inputs.content_type = "";
	invocation->break_status = 999;

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

	struct kvm_http_response resp = to_string(ctx, &chain, &to_synth_callback, (void *)ctx);
	struct http *hp = ctx->http_resp ? ctx->http_resp : ctx->http_beresp;

	status = (status >= 200 && status < 700) ? status : resp.status;
	if (status < 200 || status >= 700) {
		status = 500;
	}

#ifdef VARNISH_PLUS
	http_SetStatus(hp, status);
#else
	const char *reason = http_Status2Reason(status, NULL);
	http_SetStatus(hp, status, reason ? reason : "Unknown reason");
#endif
	http_PrintfHeader(hp, "Content-Length: %zu", resp.content_size);
	http_PrintfHeader(hp, "Content-Type: %.*s", (int)resp.ctype_size, resp.ctype);

	return (status);
}
