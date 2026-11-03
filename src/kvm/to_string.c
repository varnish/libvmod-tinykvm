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
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <vsb.h>
#include <vtim.h>
#include "vcl.h"

#include "vcc_if.h"
#include "VSC_vmod_kvm.h"

extern void kvm_varnishstat_program_cpu_time(vtim_real, vtim_real);
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

/* Fills in the response content. Returns non-zero if the callback took
   ownership of the reserved VM, which it then has to free later. */
typedef int (*content_func_f)(const struct backend_result *,
	struct kvm_http_response *, KVM_SLOT, void *);
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
	result->content_address = 0;

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
			kvm_temporarily_reserve_machine(ctx, invocation->tenant, false, invocation->soft_reset);
		if (slot == NULL) {
			/* Global program cpu-time statistic. */
			kvm_varnishstat_program_cpu_time(VTIM_real() - t0, 0.0);

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
				kvm_varnishstat_program_cpu_time(VTIM_real() - t0, 0.0);

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
			kvm_varnishstat_program_cpu_time(VTIM_real() - t0, 0.0);

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
	kvm_varnishstat_program_cpu_time(VTIM_real() - t0, 0.0);

	struct kvm_http_response resp;
	resp.status     = result->status;
	resp.ctype      = result->type;
	resp.ctype_size = result->tsize;

	/* Finalize result by calling the content callback. */
	const int kept = content_callback(result, &resp, last_slot, content_opaque);

	if (last_slot != NULL && !kept) {
		kvm_free_reserved_machine(ctx, last_slot);
	}
	return (resp);
}


static int to_string_callback(const struct backend_result *result,
	struct kvm_http_response *resp, KVM_SLOT slot, void *opaque)
{
	(void)slot;
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
		return (0);
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
	return (0);
}

VCL_STRING kvm_vm_to_string(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg, VCL_STRING on_error,
	VCL_INT error_treshold, VCL_INT soft_reset)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);
	/* break_status is 16-bit, and statuses are at most 999 */
	if (error_treshold < 0 || error_treshold > 1000)
		error_treshold = 1000;

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
	invocation->soft_reset = soft_reset;

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

	struct kvm_http_response resp = to_string(ctx, &chain, &to_string_callback, (void *)ctx);
	if (resp.status < error_treshold)
		return (resp.content);
	else
		return (on_error);
}

/* The number of VMs held for zero-copy responses, by synth() here and
   by deliver_transform() in kvm_deliver.c. While it is zero, reserving
   a VM does not have to look for a hold in the task. */
unsigned long kvm_zero_copy_holds;

#ifdef VARNISH_PLUS
extern char *kvm_synth_zero_copy(KVM_SLOT, const struct backend_result *);
/* In kvm_deliver.c, which is not part of every VMOD */
extern int kvm_deliver_release(VRT_CTX, TEN_PTR) __attribute__((weak));

/* A synth body may point straight into the memory of the VM that produced
   it. The VM is then kept reserved until the VSB has been consumed after
   vcl_synth or vcl_backend_error returns. As nothing tells us when that
   happens, the VM is freed by the next reservation of the same program
   in the task (kvm_task_release_holds()), by the next synth(), or at the
   end of the task. */
struct kvm_synth_hold {
	unsigned magic;
#define KVM_SYNTH_HOLD_MAGIC 0x5e7c0de5
	KVM_SLOT slot;
	TEN_PTR tenant;
	const struct vsb *vsb;
	const char *body;
};
/* The address is the PRIV_TASK key. It must not be ctx->req or ctx->bo,
   which hold the VM reservations of program() and vm_backend(). */
static const char kvm_synth_hold_id;

static void
kvm_synth_hold_drop(struct kvm_synth_hold *hold)
{
	AN(hold->slot);
	kvm_get_free_function()(hold->slot);
	__sync_fetch_and_sub(&kvm_zero_copy_holds, 1);
	hold->slot = NULL;
	hold->tenant = NULL;
	hold->vsb = NULL;
	hold->body = NULL;
}

static void
kvm_synth_hold_free(void *priv)
{
	struct kvm_synth_hold *hold;

	CAST_OBJ_NOTNULL(hold, priv, KVM_SYNTH_HOLD_MAGIC);
	if (hold->slot != NULL)
		kvm_synth_hold_drop(hold);
}

/* Give the VSB a buffer for len bytes (and the NUL that VSB_finish()
   writes after them), keeping the VSB_AUTOEXTEND flag so that VCL can
   still append with synthetic(). The old content is discarded. */
static char *
synth_vsb_reserve(VRT_CTX, struct vsb *vsb, size_t len)
{
	const int keep = vsb->s_flags & (VSB_DYNSTRUCT | VSB_AUTOEXTEND);
	char *buf = NULL;
	int dynamic = 0;

	if ((vsb->s_flags & VSB_DYNAMIC) && vsb->s_size > 0 &&
	    (size_t)vsb->s_size > len) {
		/* The current buffer is ours and large enough */
		buf = vsb->s_buf;
		dynamic = VSB_DYNAMIC;
	} else {
		/* Small bodies go on the workspace */
		if (len < kvm_settings.backend_early_release_size)
			buf = WS_Alloc(ctx->ws, len + 1);
		if (buf == NULL) {
			buf = (vsb->s_flags & VSB_DYNAMIC)
			    ? realloc(vsb->s_buf, len + 1) : malloc(len + 1);
			if (buf == NULL)
				return (NULL);
			dynamic = VSB_DYNAMIC;
		} else if (vsb->s_flags & VSB_DYNAMIC) {
			free(vsb->s_buf);
		}
	}
	vsb->s_buf = buf;
	vsb->s_flags = keep | dynamic;
	vsb->s_size = len + 1;
	vsb->s_len = len;
	vsb->s_error = 0;
	return (buf);
}

/* Frees the VM held by an earlier synth() in this task, if it is of the
   given program (or any program, if tenant is NULL). If the VSB still
   points into that VM, its body is copied out first, or with discard
   set, dropped, as the caller is about to replace it. Returns -1 if the
   VM could not be freed. */
static int
kvm_synth_release(VRT_CTX, TEN_PTR tenant, int discard)
{
	struct kvm_synth_hold *hold;

	/* Only look up the hold (which allocates a PRIV_TASK slot) when
	   there can be one */
	if (__atomic_load_n(&kvm_zero_copy_holds, __ATOMIC_RELAXED) == 0)
		return (0);
	if (ctx->req == NULL && ctx->bo == NULL)
		return (0);
	struct vmod_priv *priv = VRT_priv_task(ctx, &kvm_synth_hold_id);
	if (priv == NULL || priv->priv == NULL)
		return (0);
	CAST_OBJ_NOTNULL(hold, priv->priv, KVM_SYNTH_HOLD_MAGIC);
	if (hold->slot == NULL)
		return (0);
	if (tenant != NULL && hold->tenant != tenant)
		return (0);

	/* Only in vcl_synth and vcl_backend_error is ctx->specific a VSB
	   that may still be the one pointing into the VM. Otherwise that VSB
	   has already been consumed or destroyed (restart, retry). The VSB
	   pointer can only be compared, as it may have been freed. */
	if (ctx->method == VCL_MET_SYNTH || ctx->method == VCL_MET_BACKEND_ERROR) {
		struct vsb *vsb = (struct vsb *)ctx->specific;
		if (vsb == hold->vsb && vsb->s_buf == hold->body) {
			const char *body = vsb->s_buf;
			const size_t len = discard ? 0 : (size_t)vsb->s_len;
			/* The guest buffer is not VSB_DYNAMIC, so it is not freed */
			char *buf = synth_vsb_reserve(ctx, vsb, len);
			if (buf == NULL) {
				/* The VSB still points into the VM */
				VSLb(ctx->vsl, SLT_Error,
					"KVM: Out of memory for synth content (size=%zu bytes)", len);
				return (-1);
			}
			memcpy(buf, body, len);
		}
	}
	kvm_synth_hold_drop(hold);
	return (0);
}

/* Use the body in place, keeping the VM reserved. Returns 0 if it has
   to be copied instead. */
static int
synth_zero_copy(VRT_CTX, struct vsb *vsb, const struct backend_result *result,
	KVM_SLOT slot, TEN_PTR tenant)
{
	char *body = kvm_synth_zero_copy(slot, result);
	if (body == NULL)
		return (0);

	struct vmod_priv *priv = VRT_priv_task(ctx, &kvm_synth_hold_id);
	if (priv == NULL)
		return (0);
	struct kvm_synth_hold *hold = priv->priv;
	if (hold == NULL) {
		hold = WS_Alloc(ctx->ws, sizeof(*hold));
		if (hold == NULL)
			return (0);
		INIT_OBJ(hold, KVM_SYNTH_HOLD_MAGIC);
		priv->priv = hold;
		priv->len = 0;
		priv->free = kvm_synth_hold_free;
	}
	CHECK_OBJ(hold, KVM_SYNTH_HOLD_MAGIC);
	/* kvm_vm_synth() freed any earlier hold before the reservation */
	AZ(hold->slot);
	hold->slot = slot;
	hold->tenant = tenant;
	hold->vsb = vsb;
	hold->body = body;
	__sync_fetch_and_add(&kvm_zero_copy_holds, 1);

	if (vsb->s_flags & VSB_DYNAMIC)
		free(vsb->s_buf);
	vsb->s_buf = body;
	vsb->s_flags &= VSB_DYNSTRUCT | VSB_AUTOEXTEND;
	vsb->s_size = result->content_length + 1;
	vsb->s_len = result->content_length;
	vsb->s_error = 0;
	VSLb(ctx->vsl, SLT_Debug, "KVM: synth zero-copy (%zu bytes)",
		result->content_length);
	return (1);
}
#endif

/* Called before reserving a VM of the given program (kvm_vcc_api.cpp).
   A VM of that program held by this task for a zero-copy response is
   freed first, or the reservation could wait for this very task. */
int
kvm_task_release_holds(VRT_CTX, TEN_PTR tenant)
{
#ifdef VARNISH_PLUS
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(tenant);
	if (__atomic_load_n(&kvm_zero_copy_holds, __ATOMIC_RELAXED) == 0)
		return (0);
	if (kvm_synth_release(ctx, tenant, 0) < 0)
		return (-1);
	if (kvm_deliver_release != NULL && kvm_deliver_release(ctx, tenant) < 0)
		return (-1);
#else
	(void)ctx;
	(void)tenant;
#endif
	return (0);
}

struct synth_opaque {
	const struct vrt_ctx *ctx;
	TEN_PTR tenant;
	int zero_copy;
};

static int to_synth_callback(const struct backend_result *result,
	struct kvm_http_response *resp, KVM_SLOT slot, void *opaque)
{
	const struct synth_opaque *so = (const struct synth_opaque *)opaque;
	const struct vrt_ctx *ctx = so->ctx;
	CHECK_OBJ_NOTNULL(ctx->ws, WS_MAGIC);

	/* In vcl_synth or vcl_backend_error, we have access to a VSB for content */
	struct vsb *vsb = (struct vsb *)ctx->specific;
	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);

	/* Copy the content type */
	resp->ctype = result->type;
	resp->ctype_size = result->tsize;
	resp->content = NULL;
	resp->content_size = result->content_length;
	resp->writable_content = false;

#ifdef VARNISH_PLUS
	if (so->zero_copy && synth_zero_copy(ctx, vsb, result, slot, so->tenant))
		return (1);

	char *buf = synth_vsb_reserve(ctx, vsb, result->content_length);
	if (buf == NULL) {
		VSLb(ctx->vsl, SLT_Error,
			"KVM: Out of memory for synth content (size=%zu bytes)",
			result->content_length);
		VSB_clear(vsb);
		vsb->s_error = ENOMEM;
		resp->content_size = 0;
		return (0);
	}
	for (size_t i = 0; i < result->bufcount; i++) {
		memcpy(buf, result->buffers[i].data, result->buffers[i].size);
		buf += result->buffers[i].size;
	}
#else
	(void)slot;
	// There is no VSB_new in open-source Varnish
	VSB_clear(vsb);
	for (size_t i = 0; i < result->bufcount; i++) {
		VSB_bcat(vsb, result->buffers[i].data, result->buffers[i].size);
	}
#endif
	return (0);
}

VCL_INT kvm_vm_synth(VRT_CTX, VCL_PRIV task, VCL_INT status,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg, VCL_INT soft_reset,
	VCL_BOOL zero_copy)
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
	invocation->soft_reset = soft_reset;

	/* XXX: Immediately reset it. It's a thread_local! */
	struct kvm_program_chain chain = *kvm_chain_get_queue();
	kvm_chain_get_queue()->count = 0;

#ifdef VARNISH_PLUS
	/* Free any VM held by an earlier synth() in this task, of any
	   program. The body it holds is replaced by this one. */
	if (kvm_synth_release(ctx, NULL, 1) < 0)
		return (0);
#endif
	struct synth_opaque so = {
		.ctx = ctx,
		.tenant = tenant,
		.zero_copy = zero_copy,
	};
	struct kvm_http_response resp = to_string(ctx, &chain, &to_synth_callback, &so);
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
