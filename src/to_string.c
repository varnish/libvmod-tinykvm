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
#include "vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vsb.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_compute_if.h"

extern void kvm_varnishstat_program_cpu_time(vtim_real);
extern uint64_t kvm_allocate_memory(KVM_SLOT, uint64_t bytes);
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

	uint16_t status;
};
static struct kvm_http_response
minimal_response(uint16_t status, const char *ctype, const char *content)
{
	return (struct kvm_http_response) {
		.ctype = ctype,
		.ctype_size = ctype ? strlen(ctype) : 0u,
		.content = content,
		.content_size = content ? strlen(content) : 0u,
		.status = status
	};
}

static struct kvm_http_response
to_string(VRT_CTX, struct kvm_program_chain *chain)
{
	/* Program cpu-time t0. */
	const vtim_real t0 = VTIM_real();

	/* The backend_result contains many iovec-like buffers needed for
	   extracting data from the VM without copying to a temporary buffer. */
	struct backend_result *result =
		(struct backend_result *)WS_Alloc(ctx->ws, VMBE_RESULT_SIZE);
	if (result == NULL) {
		VSLb(ctx->vsl, SLT_Error, "KVM: Out of workspace for result");
		return (minimal_response(500, NULL, NULL));
	}
	result->bufcount = VMBE_NUM_BUFFERS;

	/* Post struct for chaining */
	struct backend_post *post = NULL;
	if (chain->count > 1) {
		post = (struct backend_post *)WS_Alloc(ctx->ws, sizeof(struct backend_post));
		if (post == NULL) {
			VSLb(ctx->vsl, SLT_Error, "KVM: Out of workspace for POST structure");
			return (minimal_response(500, NULL, NULL));
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
			if (last_slot != NULL) {
				kvm_free_reserved_machine(ctx, last_slot);
			}
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Unable to reserve '%s'", kvm_tenant_name(invocation->tenant));
			return (minimal_response(500, NULL, NULL));
		}

		bool use_post = false;
		if (index > 0)
		{
			/* Allocate exact bytes from previous result in reserved VM */
			post->slot = slot;
			post->address = kvm_allocate_memory(slot, result->content_length);
			post->capacity = result->content_length;
			post->length  = 0;
			post->inputs = invocation->inputs;

			if (kvm_handle_post_to_another(ctx, post, invocation, result) < 0)
			{
				kvm_free_reserved_machine(ctx, slot);
				kvm_free_reserved_machine(ctx, last_slot);
				return (minimal_response(500, NULL, NULL));
			}

			kvm_free_reserved_machine(ctx, last_slot);

			use_post = true;
		}

		/* Make a backend VM call (with optional POST). */
		kvm_backend_call(ctx, slot, invocation, use_post ? post : NULL, result);

		if (result->status >= 500) {
			kvm_free_reserved_machine(ctx, slot);
			if (last_slot != NULL) {
				kvm_free_reserved_machine(ctx, last_slot);
			}
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Error status %u from call to %s",
				result->status, kvm_tenant_name(invocation->tenant));
			return (minimal_response(500, NULL, NULL));
		}

		last_slot = slot;
	}

	/* Finalize result into a string. */
	char *content = "";
	if (result->bufcount > 0)
	{
		/* Allocate room for zero-terminated content */
		content = (char *)WS_Alloc(ctx->ws, result->content_length + 1);
		if (content == NULL) {
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Out of workspace for final content (size=%zu bytes)", result->content_length);
			return (minimal_response(500, NULL, NULL));
		}
		/* Extract content from VM */
		char *coff = content;
		for (size_t b = 0; b < result->bufcount; b++) {
			memcpy(coff, result->buffers[b].data, result->buffers[b].size);
			coff += result->buffers[b].size;
		}
		/* Zero-terminate content */
		assert(coff == &content[result->content_length]);
		*coff = 0;
	}

	/* Program cpu-time statistic. */
	kvm_varnishstat_program_cpu_time(VTIM_real() - t0);

	struct kvm_http_response res;
	res.status     = result->status;
	res.ctype      = result->type;
	res.ctype_size = result->tsize;
	res.content    = content;
	res.content_size = result->content_length;

	if (last_slot != NULL) {
		kvm_free_reserved_machine(ctx, last_slot);
	}
	return (res);
}

VCL_STRING kvm_vm_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg, VCL_STRING on_error)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Program not found: %s", program);
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

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

	struct kvm_http_response resp = to_string(ctx, &chain);
	if (resp.status < 500 && resp.content != NULL)
		return (resp.content);
	else
		return (on_error);
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

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

	struct kvm_http_response resp = to_string(ctx, &chain);
	struct http *hp = ctx->http_resp ? ctx->http_resp : ctx->http_beresp;

	status = (status >= 200 && status < 700) ? status : resp.status;
	http_SetStatus(hp, status);
	http_PrintfHeader(hp, "Content-Length: %zu", resp.content_size);
	http_PrintfHeader(hp, "Content-Type: %.*s", (int)resp.ctype_size, resp.ctype);

	struct vsb *vsb = (struct vsb *)ctx->specific;
	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);
	VSB_delete(vsb);

	/* This is unfortunately a double copy. If we return a fixed-length VSB,
	   varnish will crash if return(deliver) is not used immediately after. */
	vsb = VSB_new_auto();
	((struct vrt_ctx *)ctx)->specific = vsb;
	VSB_bcat(vsb, resp.content, resp.content_size);

	return (status);
}
