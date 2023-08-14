/**
 * @file kvm_backend.c
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2022-10-10
 *
 *
 * This file contains all the glue between Varnish backend functionality
 * and the VMOD KVM tenant VMs. Here we create a backend that can POST
 * data into a tenants VM and then extract data from and return that as
 * a HTTP response.
 *
 * The vmod_vm_backend function is the VCL function that selected VMOD KVM
 * as a backend for a specific URL, usually tied to a Host header field.
 * Once selected, Varnish will eventually switch over to the backend side,
 * by activating a backend worker thread, and then invoke the function
 * registered by the .gethdrs callback. That is, kvmbe_gethdrs.
 *
 */
#include "vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_if.h"
extern uint64_t kvm_allocate_memory(KVM_SLOT, uint64_t bytes);
extern void kvm_backend_call(VRT_CTX, KVM_SLOT,
	struct kvm_chain_item *, struct backend_post *, struct backend_result *);
extern int kvm_get_body(struct backend_post *, struct busyobj *);
extern int kvm_backend_streaming_post(struct backend_post *, const void*, ssize_t);
__thread struct kvm_program_chain kqueue;

static inline void kvm_ts(struct vsl_log *vsl, const char *event,
						  double *work, double *prev)
{
	VSLb_ts(vsl, event, *work, prev, VTIM_real());
}

static void v_matchproto_(vdi_panic_f)
kvmbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

static void v_matchproto_(vdi_finish_f)
kvmbe_finish(const struct director *dir, struct worker *wrk, struct busyobj *bo)
{
	(void) wrk;
	(void) dir;

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = NULL;
	bo->htc->magic = 0;
	bo->htc = NULL;
}

static enum vfp_status v_matchproto_(vfp_pull_f)
kvmfp_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	(void)vc;

	/* The resulting response of a backend request. */
	struct backend_result *result = (struct backend_result *)vfe->priv1;
	if (result->content_length == 0) {
		*lp = 0;
		return (VFP_END);
	}

	/* Go through each response buffer and write it to storage. */
	struct VMBuffer *current = &result->buffers[vfe->priv2];
	ssize_t max = *lp;
	ssize_t written = 0;

	while (1) {
		ssize_t len = (current->size > max) ? max : current->size;
		memcpy(p, current->data, len);
		p = (void *) ((char *)p + len);
		current->data += len;
		current->size -= len;
		written += len;

		/* Go to next buffer, or end if no more */
		if (current->size == 0) {
			/* Go to next buffer */
			vfe->priv2 ++;
			/* Reaching bufcount means end of fetch */
			if (vfe->priv2 == (ssize_t)result->bufcount) {
				assert(current->size == 0);
				*lp = written;
				return (VFP_END);
			}
			current = &result->buffers[vfe->priv2];
		}
		/* Return later if there's more, and we can't send more */
		max -= len;
		if (max == 0) {
			assert(vfe->priv2 < (ssize_t)result->bufcount);
			*lp = written;
			return (VFP_OK);
		}
	}
}
static const struct vfp kvm_fetch_processor = {
	.name = "kvm_backend",
	.pull = kvmfp_pull,
};

#include "kvm_streaming_backend.c"


static void
vfp_init(struct busyobj *bo, bool streaming)
{
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	struct vfp_entry *vfe;

	if (!streaming) {
		vfe = VFP_Push(bo->vfc, &kvm_fetch_processor);
	} else {
		vfe = VFP_Push(bo->vfc, &kvm_streaming_fetch_processor);
	}

	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	vfe->priv1 = bo->htc->priv;
	vfe->priv2 = 0;
}

static int
kvmbe_write_response(struct busyobj *bo,
	VRT_CTX, struct backend_result* result)
{
	/* Status code is sanitized in the backend call. */
	http_PutResponse(bo->beresp, "HTTP/1.1", result->status, NULL);

	/* Always set content-length, always known. */
	http_PrintfHeader(bo->beresp,
		"Content-Length: %zu", result->content_length);

	/* TODO: Tie Last-Modified to content? */
	char timestamp[VTIM_FORMAT_SIZE];
	VTIM_format(VTIM_real(), timestamp);
	http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

	/* HTTP connection of BusyObj on workspace. */
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL) {
		VSLb(ctx->vsl, SLT_Error, "KVM: Out of workspace for HTC");
		return (-1);
	}
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* Store the result in workspace and free result. */
	bo->htc->content_length = result->content_length;
	bo->htc->priv = (void *)result;
	bo->htc->body_status = BS_LENGTH;

	/* Initialize fetch processor, which will retrieve the data from
	   the VM, buffer by buffer, and send it to Varnish storage.
	   It is a streamed response if the buffer-count is zero, but the
	   content-length is non-zero. We also must have a callback function.
	   See: kvmfp_pull, kvmfp_streaming_pull */
	const bool streaming = result->content_length > 0 && result->bufcount == 0;
	vfp_init(bo, streaming);
	return (0);
}

static int v_matchproto_(vdi_gethdrs_f)
kvmbe_gethdrs(const struct director *dir,
	struct worker *wrk, struct busyobj *bo)
{
	(void)wrk;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);

	/* Retrieve info from struct filled by VCL vm_backend(...) call. */
	struct vmod_kvm_backend *kvmr;
	CAST_OBJ_NOTNULL(kvmr, dir->priv, KVM_BACKEND_MAGIC);

	/* Create a fake VRT_CTX in order to have access to VCL,
	   workspace, VSL, BusyObj and the request later on. */
	const struct vrt_ctx ctx = {
		.magic = VRT_CTX_MAGIC,
		.vcl = bo->vcl,
		.ws  = bo->ws,
		.vsl = bo->vsl,
		.req = NULL,
		.bo  = bo,
		.http_bereq  = bo->bereq,
		.http_beresp = bo->beresp,
	};

	/* Request body first, then VM result */
	struct backend_post *post = NULL;
	const bool is_post =
		(bo->initial_req_body_status != REQ_BODY_NONE || bo->bereq_body != NULL);
	if (is_post || kvmr->chain.count > 0)
	{
		/* Initialize POST. */
		post = (struct backend_post *)WS_Alloc(bo->ws, sizeof(struct backend_post));
		if (post == NULL) {
			VSLb(ctx.vsl, SLT_Error, "KVM: Out of workspace for request body");
			return (-1);
		}
		post->ctx = &ctx;
	}

	/* The backend_result contains many iovec-like buffers needed for
	   extracting data from the VM without copying to a temporary buffer. */
	struct backend_result *result =
		(struct backend_result *)WS_Alloc(bo->ws, VMBE_RESULT_SIZE);
	if (result == NULL) {
		VSLb(ctx.vsl, SLT_Error, "KVM: Out of workspace for result");
		return (-1);
	}
	result->bufcount = VMBE_NUM_BUFFERS;

	/* Loop through each program in the chain */
	for (int index = 0; index < kvmr->chain.count; index++)
	{
		/* The chain item contains the tenant program and all the inputs
		   to the program. Everything needed to invoke the VM function. */
		struct kvm_chain_item *invocation =
			&kvmr->chain.chain[index];

		if (VMOD_KVM_BACKEND_TIMINGS) {
			kvmr->t_prev = bo->t_prev;
			kvm_ts(ctx.vsl, "TenantStart", &kvmr->t_work, &kvmr->t_prev);
		}

		/* Reserving a VM means putting ourselves in a concurrent queue
		waiting for a free VM, and then getting exclusive access until
		the end of the request. */
		struct vmod_kvm_slot *slot =
			kvm_reserve_machine(&ctx, invocation->tenant, kvmr->debug);
		if (slot == NULL) {
			VSLb(ctx.vsl, SLT_Error, "KVM: Unable to reserve machine");
			return (-1);
		}
		if (VMOD_KVM_BACKEND_TIMINGS) {
			kvm_ts(ctx.vsl, "TenantReserve", &kvmr->t_work, &kvmr->t_prev);
		}

		/* HTTP method */
		invocation->inputs.method = bo->bereq->hd[0].b;
		invocation->inputs.content_type = "";

		/* Gather request body data if it exists. Check taken from stp_fetch.
		If the body is cached already, bereq_body will be non-null.
		NOTE: We only read the request body for index == 0. */
		bool use_post = false;
		if (is_post && index == 0)
		{
			/* Retrieve body by copying directly into backend VM. */
			post->slot = slot;
			post->address = kvm_allocate_memory(slot, POST_BUFFER); /* Buffer bytes */
			post->capacity = POST_BUFFER;
			post->length  = 0;
			post->inputs = invocation->inputs;
			int ret = kvm_get_body(post, bo);
			if (ret < 0) {
				VSLb(ctx.vsl, SLT_Error, "KVM: Unable to aggregate request body data");
				return (-1);
			}
			if (VMOD_KVM_BACKEND_TIMINGS) {
				kvm_ts(ctx.vsl, "TenantRequestBody", &kvmr->t_work, &kvmr->t_prev);
			}
			/* Get *initial* Content-Type from backend request. */
			if (!http_GetHdr(ctx.http_bereq, H_Content_Type, &invocation->inputs.content_type))
				invocation->inputs.content_type = "";
			use_post = true;
		}
		else if (index > 0)
		{
			/* Allocate exact bytes from previous result in reserved VM */
			post->slot = slot;
			post->address = kvm_allocate_memory(slot, result->content_length);
			post->capacity = result->content_length;
			post->length  = 0;
			post->inputs = invocation->inputs;

			/* index > 0: Shuffle data between VMs */
			for (size_t b = 0; b < result->bufcount; b++)
			{
				const struct VMBuffer *buffer = &result->buffers[b];
				/* Streaming POST is essentially copying data into the VM.
				   If streaming is enabled, a callback will be invoked on the
				   *current* program with the data from the previous.
				   Otherwise, it's just a straight copy into a sequential buffer. */
				if (kvm_backend_streaming_post(post, buffer->data, buffer->size) < 0)
				{
					VSLb(ctx.vsl, SLT_Error,
						"KVM: Unable to stream data (status=%u) to %s in chain",
						result->status, kvm_tenant_name(invocation->tenant));
					return (-1);
				}
			}
			/* Reset total buffer count in order to make another VM call */
			result->bufcount = VMBE_NUM_BUFFERS;

			if (VMOD_KVM_BACKEND_TIMINGS) {
				kvm_ts(ctx.vsl, "TenantRequestBody", &kvmr->t_work, &kvmr->t_prev);
			}

			/* Forward the Content-Type as a direct argument to the next program. */
			if ((invocation->inputs.content_type = result->type) == NULL)
				invocation->inputs.content_type = "";
			use_post = true;
		}

		/* Make a backend VM call (with optional POST). */
		kvm_backend_call(&ctx, slot, invocation, use_post ? post : NULL, result);

		if (VMOD_KVM_BACKEND_TIMINGS) {
			kvm_ts(ctx.vsl, "TenantProcess", &kvmr->t_work, &kvmr->t_prev);
		}

		if (result->status >= 500) {
			VSLb(ctx.vsl, SLT_Error,
				"KVM: Error status %u from call to %s at index %d in chain",
				result->status, kvm_tenant_name(invocation->tenant), index);
			break;
		}

		/* Explicitly set content-type when present. This allows
		   other programs in the chain to read it as needed.
		   An empty content-type is treated as no-change. Programs
		   and VCL can unset the content-type if needed. */
		if (result->tsize > 0)
		{
			// If the Content-Type is different,
			if (!http_HdrIs(bo->beresp, H_Content_Type, result->type))
			{
				// Unset old one, and set new one
				http_Unset(bo->beresp, H_Content_Type);
				http_PrintfHeader(bo->beresp,
					"%s %.*s", H_Content_Type + 1, (int)result->tsize, result->type);
			}
		}
	}

	/* Finish the response.
	   After the last function call, the result buffer is filled with
	   the last result, etc. Send backend response to varnish storage. */
	const int res = kvmbe_write_response(
		bo, &ctx, result);

	if (VMOD_KVM_BACKEND_TIMINGS) {
		kvm_ts(ctx.vsl, "TenantResponse", &kvmr->t_work, &kvmr->t_prev);
	}

	return (res);
}

static void init_director(VRT_CTX, struct vmod_kvm_backend *kvmr)
{
	kvmr->dir = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_backend));
	if (kvmr->dir == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace");
		return;
	}

	struct director *dir = kvmr->dir;
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmr;
	dir->name = "KVM backend director";
	dir->vcl_name = "vmod_kvm";
	dir->gethdrs = kvmbe_gethdrs;
	dir->finish  = kvmbe_finish;
	dir->panic   = kvmbe_panic;
}
static int init_chain(VRT_CTX, struct vmod_kvm_tenant *tenant,
	const char *url, const char *arg)
{
	(void)ctx;

	struct kvm_chain_item *item = &kqueue.chain[kqueue.count];
	item->tenant = tenant;
	item->inputs.method = "";
	item->inputs.url = url ? url : "";
	item->inputs.argument = arg ? arg : "";
	kqueue.count++;
	return (1);
}
static void init_kvmr(struct vmod_kvm_backend *kvmr)
{
	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	if (VMOD_KVM_BACKEND_TIMINGS) {
		kvmr->t_work = VTIM_real();
	}
	kvmr->chain = kqueue;
	/* XXX: Immediately reset it. It's a thread_local! */
	kqueue.count = 0;
}

void vmod_kvm_set_kvmr_backend(struct director *dir, VCL_BACKEND backend)
{
	struct vmod_kvm_backend *kvmr = (struct vmod_kvm_backend *)dir->priv;
	kvmr->backend = backend;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx, "vmod_kvm: vm_backend() should only"
		    "be called from vcl_backend_fetch");
		return (NULL);
	}

	/* Everything we do has lifetime of the backend request,
	   so we can use the workspace. */
	struct vmod_kvm_backend *kvmr =
		WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_backend));
	if (kvmr == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace (kvm_backend)");
		return (NULL);
	}

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Tenant not found: %s", program);
		return (NULL);
	}

	/* Cannot fail at this point, add to chain */
	if (!init_chain(ctx, tenant, url, arg)) {
		return (NULL);
	}

	init_kvmr(kvmr);

	init_director(ctx, kvmr);
	return (kvmr->dir);
}

VCL_BACKEND vmod_vm_debug_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING key, VCL_STRING url, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx, "vmod_kvm: vm_debug_backend() should only"
		    "be called from vcl_backend_fetch");
		return (NULL);
	}

	/* Everything we do has lifetime of the backend request,
	   so we can use the workspace. */
	struct vmod_kvm_backend *kvmr =
		WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_backend));
	if (kvmr == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace (kvm_backend)");
		return (NULL);
	}

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find_key(task, program, key);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Tenant not found: %s", program);
		return (NULL);
	}
	if (!kvm_tenant_debug_allowed(tenant)) {
		VRT_fail(ctx, "KVM: Tenant not allowed to live-debug: %s", program);
		return (NULL);
	}

	/* Cannot fail at this point, add to chain */
	if (!init_chain(ctx, tenant, url, arg)) {
		return (NULL);
	}

	init_kvmr(kvmr);
	kvmr->debug = 1;

	init_director(ctx, kvmr);
	return (kvmr->dir);
}

VCL_BOOL vmod_chain(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx, "compute: chain() should only be called from vcl_backend_fetch");
		return (0);
	}

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Tenant not found: %s", program);
		return (0);
	}

	/* Cannot fail at this point, add to chain */
	if (!init_chain(ctx, tenant, url, arg)) {
		return (0);
	}

	return (1);
}
