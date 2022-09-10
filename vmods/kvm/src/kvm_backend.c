/**
 * @file kvm_backend.c
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2022-07-23
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
	const char *farg[2], struct backend_post *, struct backend_result *);
extern int kvm_get_body(struct backend_post *, struct busyobj *);

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

	kvmr->t_prev = bo->t_prev;
	kvm_ts(ctx.vsl, "TenantStart", &kvmr->t_work, &kvmr->t_prev, VTIM_real());

	/* Reserving a VM means putting ourselves in a concurrent queue
	   waiting for a free VM, and then getting exclusive access until
	   the end of the request. */
	struct vmod_kvm_slot *slot =
		kvm_reserve_machine(&ctx, kvmr->tenant, kvmr->debug);
	if (slot == NULL) {
		VSLb(ctx.vsl, SLT_Error, "KVM: Unable to reserve machine");
		return (-1);
	}
	kvm_ts(ctx.vsl, "TenantReserve", &kvmr->t_work, &kvmr->t_prev, VTIM_real());

	struct backend_post *post = NULL;
	/* The HTTP method is stored in the first header field.
	   We may want to change the API, passing the method as argument. */
	const bool is_post = strcmp(bo->bereq->hd[0].b, "POST") == 0;
	if (is_post)
	{
		/* Retrieve body by copying directly into backend VM. */
		post = (struct backend_post *)WS_Alloc(bo->ws, sizeof(struct backend_post));
		if (post == NULL) {
			VSLb(ctx.vsl, SLT_Error, "KVM: Out of workspace for post");
			return (-1);
		}
		post->ctx = &ctx;
		post->slot = slot;
		post->address = kvm_allocate_memory(slot, POST_BUFFER); /* Buffer bytes */
		post->capacity = POST_BUFFER;
		post->length  = 0;
		post->argument = kvmr->funcarg[0];
		int ret = kvm_get_body(post, bo);
		if (ret < 0) {
			VSLb(ctx.vsl, SLT_Error, "KVM: Unable to aggregate POST data");
			return (-1);
		}
		kvm_ts(ctx.vsl, "TenantRequestBody", &kvmr->t_work, &kvmr->t_prev, VTIM_real());
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

	/* Make a backend VM call (with optional POST). */
	kvm_backend_call(&ctx, slot, kvmr->funcarg, post, result);
	kvm_ts(ctx.vsl, "TenantProcess", &kvmr->t_work, &kvmr->t_prev, VTIM_real());

	/* Status code is sanitized in the backend call. */
	http_PutResponse(bo->beresp, "HTTP/1.1", result->status, NULL);

	/* Explicitly set content-type when known. */
	if (result->tsize > 0)
	{
		http_PrintfHeader(bo->beresp, "Content-Type: %.*s",
			(int) result->tsize, result->type);
		http_PrintfHeader(bo->beresp,
			"Content-Length: %zu", result->content_length);
	}

	/* TODO: Tie Last-Modified to content? */
	char timestamp[VTIM_FORMAT_SIZE];
	VTIM_format(VTIM_real(), timestamp);
	http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

	/* HTTP connection of BusyObj on workspace. */
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL) {
		VSLb(ctx.vsl, SLT_Error, "KVM: Out of workspace for HTC");
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
	kvm_ts(ctx.vsl, "TenantResponse", &kvmr->t_work, &kvmr->t_prev, VTIM_real());
	return (0);
}

static void init_director(struct director *dir, void *kvmr)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmr;
	dir->name = "KVM backend director";
	dir->vcl_name = "vmod_kvm";
	dir->gethdrs = kvmbe_gethdrs;
	dir->finish  = kvmbe_finish;
	dir->panic   = kvmbe_panic;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING url, VCL_STRING arg)
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
		VRT_fail(ctx, "KVM: Out of workspace");
		return (NULL);
	}

	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	kvmr->t_work = VTIM_real();

	/* Lookup internal tenant using VCL task */
	kvmr->tenant = kvm_tenant_find(task, tenant);
	if (kvmr->tenant == NULL) {
		VRT_fail(ctx, "KVM: Tenant not found: %s", tenant);
		return (NULL);
	}

	kvmr->funcarg[0] = url ? url : "";
	kvmr->funcarg[1] = arg ? arg : "";
	kvmr->debug = 0;
	kvmr->max_response_size = 0;

	init_director(&kvmr->dir, kvmr);
	return (&kvmr->dir);
}

VCL_BACKEND vmod_vm_debug_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING key, VCL_STRING url, VCL_STRING arg)
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
		VRT_fail(ctx, "KVM: Out of workspace");
		return (NULL);
	}

	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	kvmr->t_work = VTIM_real();

	/* Lookup internal tenant using VCL task */
	kvmr->tenant = kvm_tenant_find_key(task, tenant, key);
	if (kvmr->tenant == NULL) {
		VRT_fail(ctx, "KVM: Tenant not found: %s", tenant);
		return (NULL);
	}
	if (!kvm_tenant_debug_allowed(kvmr->tenant)) {
		VRT_fail(ctx, "KVM: Tenant not allowed to live-debug: %s", tenant);
		return (NULL);
	}

	kvmr->funcarg[0] = url ? url : "";
	kvmr->funcarg[1] = arg ? arg : "";
	kvmr->debug = 1;
	kvmr->max_response_size = 0;

	init_director(&kvmr->dir, kvmr);
	return (&kvmr->dir);
}
