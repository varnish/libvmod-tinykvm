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
#include "VSC_vmod_kvm.h"

extern void kvm_varnishstat_program_cpu_time(vtim_real, vtim_real);
extern void kvm_backend_call(VRT_CTX, KVM_SLOT,
	const struct kvm_chain_item *, struct backend_post *, struct backend_result *);
extern int kvm_get_body(struct backend_post *, struct busyobj *);
extern int kvm_backend_streaming_post(struct backend_post *, const void*, ssize_t);
__thread struct kvm_program_chain kqueue;

#ifndef VARNISH_PLUS
static const struct vmod_priv_methods kvm_priv_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "vmod_kvm",
	.fini = kvm_free_reserved_machine
}};
#endif

struct kvm_program_chain* kvm_chain_get_queue()
{
	return &kqueue;
}

static inline struct vmod_priv *
kvm_get_priv_task(VRT_CTX)
{
	if (ctx->req) {
		return VRT_priv_task(ctx, ctx->req);
	} else {
		return VRT_priv_task(ctx, ctx->bo);
	}
}

static int
kvm_release_after_request(VRT_CTX, KVM_SLOT slot)
{
	struct vmod_priv* priv_task = kvm_get_priv_task(ctx);
	priv_task->priv = slot;
	priv_task->len  = 0;
#ifdef VARNISH_PLUS
	priv_task->free = kvm_get_free_function();
#else
	priv_task->methods = kvm_priv_methods;
#endif
	return (1);
}
static void
kvm_early_slot_release(struct vmod_priv *priv_task)
{
	AN(priv_task);
#ifdef VARNISH_PLUS
	if (priv_task->free)
		priv_task->free(priv_task->priv);
	priv_task->free = NULL;
#else
	if (priv_task->methods)
		priv_task->methods->fini(NULL, priv_task->priv);
	priv_task->methods = NULL;
#endif
	priv_task->priv = NULL;
	priv_task->len  = 0;
}

static void v_matchproto_(vdi_panic_f)
kvmbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

#ifdef VARNISH_PLUS
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
#else
static void v_matchproto_(vdi_finish_f)
kvmbe_finish(VRT_CTX, const struct director *dir)
{
	(void)dir;

	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct busyobj *bo = ctx->bo;

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = NULL;
	bo->htc->magic = 0;
	bo->htc = NULL;
}
#endif

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
	if (result->status < 200)
		result->status = 500;
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
#ifndef VARNISH_PLUS
	bo->htc->doclose = SC_REM_CLOSE;
#endif

	/* Initialize fetch processor, which will retrieve the data from
	   the VM, buffer by buffer, and send it to Varnish storage.
	   It is a streamed response if the buffer-count is zero, but the
	   content-length is non-zero. We also must have a callback function.
	   See: kvmfp_pull, kvmfp_streaming_pull */
	const bool streaming = result->content_length > 0 && result->bufcount == 0;
	vfp_init(bo, streaming);
	return (0);
}

int kvm_handle_post_to_another(VRT_CTX, struct backend_post *post,
	struct kvm_chain_item *invocation, struct backend_result *result)
{
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
			VSLb(ctx->vsl, SLT_Error,
				"KVM: Unable to stream data (status=%u) to %s in chain",
				result->status, kvm_tenant_name(invocation->tenant));
			return (-1);
		}
	}
	/* Reset total buffer count in order to make another VM call */
	result->bufcount = VMBE_NUM_BUFFERS;

	/* Forward the Content-Type as a direct argument to the next program. */
	if (result->tsize == 0) {
		invocation->inputs.content_type = "";
	} else {
		/* Duplicate the Content-Type string. */
		char *ctype = (char*)WS_Alloc(ctx->ws, result->tsize + 1);
		if (ctype != NULL) {
			memcpy(ctype, result->type, result->tsize);
			ctype[result->tsize] = 0;
			invocation->inputs.content_type = ctype;
		} else {
			invocation->inputs.content_type = "";
		}
	}
	invocation->inputs.method = "POST";
	return (0);
}

#ifdef VARNISH_PLUS
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
#else
static int v_matchproto_(vdi_gethdrs_f)
kvmbe_gethdrs(const struct vrt_ctx *other_ctx, const struct director *dir)
{
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	struct busyobj *bo = other_ctx->bo;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);
#endif
	/* Program cpu-time t0. */
	const vtim_real t0 = VTIM_real();
	vtim_real self_request_time = 0.0;

	/* Retrieve info from struct filled by VCL vm_backend(...) call. */
	struct vmod_kvm_backend *kvmr;
	CAST_OBJ_NOTNULL(kvmr, dir->priv, KVM_BACKEND_MAGIC);

	/* Create a fake VRT_CTX in order to have access to VCL,
	   workspace, VSL, BusyObj and the request later on. */
	const struct vrt_ctx ctx = {
		.magic = VRT_CTX_MAGIC,
		.vcl = bo->vcl,
#ifndef VARNISH_PLUS
		.vpi = bo->wrk->vpi,
#endif
		.ws  = bo->ws,
		.vsl = bo->vsl,
		.req = NULL,
		.bo  = bo,
		.http_bereq  = bo->bereq,
		.http_beresp = bo->beresp,
	};

	/* Request body first, then VM result */
	struct backend_post *post = NULL;
#ifdef VARNISH_PLUS
	const bool is_post =
		(bo->initial_req_body_status != REQ_BODY_NONE || bo->bereq_body != NULL);
#else
	/// XXX: Is this correct?
	const bool is_post = (bo->bereq_body != NULL
		|| (bo->req != NULL && bo->req->req_body_status != NULL && bo->req->req_body_status->avail));
#endif
	if (is_post || kvmr->chain.count > 1)
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

	/* Temporary VM reservation that can be re-used. */
	struct vmod_kvm_slot *last_slot = NULL;
	TEN_PTR last_tenant = NULL;
	void* must_free_chunk = NULL;

	#define LOOP_EXIT_ACTIONS() \
		free(must_free_chunk);  \
		must_free_chunk = NULL; \
		if (last_slot != NULL)  \
			kvm_free_reserved_machine(&ctx, last_slot);


	/* Loop through each program in the chain */
	for (int index = 0; index < kvmr->chain.count; index++)
	{
		const bool is_temporary = (index+1 < kvmr->chain.count);

		/* The chain item contains the tenant program and all the inputs
		   to the program. Everything needed to invoke the VM function. */
		struct kvm_chain_item *invocation =
			&kvmr->chain.chain[index];

		if (invocation->special_function != NULL)
		{
			/* Only self-request fetches for now. */
			const vtim_real srt0 = VTIM_real();
			int res = kvm_self_request(&ctx,
				invocation->inputs.url, invocation->inputs.argument, result);
			self_request_time = VTIM_real() - srt0;

			if (res < 0 || result->status >= 400)
				break;
			/* Only used to free the chunk after usage. */
			must_free_chunk = TRUST_ME(result->buffers[0].data);
			/* Straight to next in chain. */
			continue;
		}

		/* Reserving a VM means putting ourselves in a concurrent queue
		waiting for a free VM, and then getting exclusive access until
		the end of the request. */
		struct vmod_kvm_slot *slot;
		if (last_slot != NULL && last_tenant == invocation->tenant) {
			/* Re-use the last reservation if same program. */
			slot = last_slot;
			last_slot = NULL;
			/* Work-around for when the last two programs are the same,
			   avoiding a situation where we never give back the VM reservation. */
			if (!is_temporary) {
				if (!kvm_release_after_request(&ctx, slot)) {
					kvm_free_reserved_machine(&ctx, slot);
					slot = NULL;
				}
			}
		} else if (is_temporary) {
			/* This is in the middle of a chain, temporary reservation. */
			slot = kvm_temporarily_reserve_machine(&ctx, invocation->tenant, kvmr->debug);
		} else {
			/* The last in the chain is a full reservation. */
			slot = kvm_reserve_machine(&ctx, invocation->tenant, kvmr->debug);
		}
		if (slot == NULL) {
			/* Let go of any previous program in the chain. */
			LOOP_EXIT_ACTIONS();

			VSLb(ctx.vsl, SLT_Error,
				"KVM: Unable to reserve VM for index %d, program %s",
				index, kvm_tenant_name(invocation->tenant));
			return (-1);
		}

		/* Gather request body data if it exists. Check taken from stp_fetch.
		If the body is cached already, bereq_body will be non-null.
		NOTE: We only read the request body for index == 0. */
		bool use_post = false;
		if (is_post && index == 0)
		{
			/* Retrieve body by copying directly into backend VM. */
			post->slot = slot;
			post->address = 0;
			post->capacity = POST_BUFFER;
			post->length  = 0;
			post->inputs = invocation->inputs;
			use_post = true;

			int ret = kvm_get_body(post, bo);
			if (ret < 0) {
				VSLb(ctx.vsl, SLT_Error,
					"KVM: Unable to aggregate request body data for index %d, program %s",
					index, kvm_tenant_name(invocation->tenant));
				// There is no previous in the chain to free
				if (is_temporary)
					kvm_free_reserved_machine(&ctx, slot);
				return (-1);
			}
		}
		else if (index > 0)
		{
			/* Allocate exact bytes from previous result in reserved VM */
			post->slot = slot;
			post->address = 0;
			post->capacity = result->content_length;
			post->length  = 0;
			post->inputs = invocation->inputs;

			/* This marks the end of the previous request (in the chain). */
			if (kvm_handle_post_to_another(&ctx, post, invocation, result) < 0) {
				// There is a previous in the chain to free, if not the same program
				LOOP_EXIT_ACTIONS();
				if (is_temporary)
					kvm_free_reserved_machine(&ctx, slot);
				VSLb(ctx.vsl, SLT_Error,
					"KVM: Unable to transfer POST data for index %d, program %s",
					index, kvm_tenant_name(invocation->tenant));
				return (-1);
			}

			kvm_free_reserved_machine(&ctx, last_slot);

			use_post = true;
		}
		/* At this point any previously reserved VM has been freed.
		   We are only reserving the current VM in the chain. */
		last_slot = NULL;

		/* Make a backend VM call (with optional POST). */
		kvm_backend_call(&ctx, slot, invocation, use_post ? post : NULL, result);

		/* Setting last_slot here enables short-response optimization for errors. */
		last_tenant = invocation->tenant;
		last_slot = slot;

		if (result->status >= invocation->break_status) {
			VSLb(ctx.vsl, SLT_Error,
				"KVM: Error status %u from call to %s at index %d in chain",
				result->status, kvm_tenant_name(invocation->tenant), index);
			if (is_temporary)
				kvm_free_reserved_machine(&ctx, slot);
			break;
		}
	}

	/* Final content-type. */
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
	} else {
		http_Unset(bo->beresp, H_Content_Type);
	}

	/**
	 * It is possible to release the VM early if we can store the
	 * result on the workspace directly. Only for short responses.
	*/
	if (last_slot != NULL && result->content_length < kvm_settings.backend_early_release_size)
	{
		if (result->content_length == 0)
		{
			result->bufcount = 1;
			result->buffers[0].data = NULL;
			result->buffers[0].size = 0;
		}
		else
		{
			char *cnt = (char *)WS_Alloc(ctx.ws, result->content_length);
			if (cnt != NULL)
			{
				size_t len = 0;
				for (size_t i = 0; i < result->bufcount; i++) {
					memcpy(&cnt[len], result->buffers[i].data, result->buffers[i].size);
					len += result->buffers[i].size;
				}
				assert(len == result->content_length);

				result->bufcount = 1;
				result->buffers[0].data = cnt;
				result->buffers[0].size = len;
			}
		}
		/* We don't need to hold the VM reservation anymore.
		NOTE: result is already on the workspace */
		kvm_early_slot_release(kvm_get_priv_task(&ctx));
	}

	free(must_free_chunk);

	/* Program cpu-time statistic. */
	const vtim_real cputime = VTIM_real() - t0 - self_request_time;
	kvm_varnishstat_program_cpu_time(cputime >= 0.0 ? cputime : 0.0, self_request_time);

	/* Finish the response.
	   After the last function call, the result buffer is filled with
	   the last result. Send backend response to varnish storage. */
	const int res = kvmbe_write_response(
		bo, &ctx, result);

	return (res);
}

#ifndef VARNISH_PLUS
static const struct vdi_methods kvm_director_methods[1] = {{
	.magic = VDI_METHODS_MAGIC,
	.type  = "kvm_director_methods",
	.gethdrs = kvmbe_gethdrs,
	.finish  = kvmbe_finish,
}};
#endif

static void init_director(VRT_CTX, struct vmod_kvm_backend *kvmr)
{
	struct director *dir =
		WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_backend));
	if (dir == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace for director");
		return;
	}

	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmr;
	dir->vcl_name = "vmod_kvm";
	kvmr->dir = dir;

#ifdef VARNISH_PLUS
	dir->name = "KVM backend director";
	dir->gethdrs = kvmbe_gethdrs;
	dir->finish  = kvmbe_finish;
	dir->panic   = kvmbe_panic;
#else
	struct vcldir *vdir =
		WS_Alloc(ctx->ws, sizeof(struct vcldir));
	if (vdir == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace for internal director");
		return;
	}
	memset(vdir, 0, sizeof(*vdir));
	vdir->magic = VCLDIR_MAGIC;
	vdir->dir = dir;
	vdir->vcl = ctx->vcl;
	vdir->flags |= VDIR_FLG_NOREFCNT;
	vdir->methods = kvm_director_methods;

	dir->vdir = vdir;
#endif
}
struct kvm_chain_item *
kvm_init_chain(VRT_CTX, struct vmod_kvm_tenant *tenant, const char *url, const char *arg)
{
	(void)ctx;
	struct kvm_program_chain *kqueue = kvm_chain_get_queue();
	if (kqueue->count >= KVM_PROGRAM_CHAIN_ENTRIES)
		return (NULL);

	struct kvm_chain_item *item = &kqueue->chain[kqueue->count];
	item->tenant = tenant;
	item->special_function = NULL;
	item->inputs.url = url ? url : "";
	item->inputs.argument = arg ? arg : "";
	item->break_status = 1000; /* No breaking for last program. */

	if (kqueue->count == 0) {
		struct http *hp;
		if (ctx->http_bereq)
			hp = ctx->http_bereq;
		else
			hp = ctx->http_req;
		if (hp == NULL)
			return (NULL);
		CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

		item->inputs.method = hp->hd[0].b;
		if (!http_GetHdr(hp, H_Content_Type, &item->inputs.content_type))
			item->inputs.content_type = "";
	} else {
		item->inputs.method = "";
		item->inputs.content_type = "";
	}
	kqueue->count++;
	return (item);
}
struct kvm_chain_item *
kvm_init_fetch(VRT_CTX, const char *url, const char *arg)
{
	(void)ctx;
	struct kvm_program_chain *kqueue = kvm_chain_get_queue();
	if (kqueue->count != 0 || url == NULL)
		return (NULL);

	struct kvm_chain_item *item = &kqueue->chain[kqueue->count];
	item->tenant = NULL;
	item->special_function = "fetch";
	item->inputs.url = url; /* Cannot be NULL. */
	item->inputs.argument = arg ? arg : "";
	item->inputs.method = "";
	item->inputs.content_type = "";

	kqueue->count++;
	return (item);
}
static void init_kvmr(struct vmod_kvm_backend *kvmr)
{
	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	kvmr->chain = kqueue;
	/* XXX: Immediately reset it. It's a thread_local! */
	kqueue.count = 0;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

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
		VRT_fail(ctx, "KVM: Out of workspace for kvm_backend");
		return (NULL);
	}

	/* Lookup internal tenant using VCL task */
	struct vmod_kvm_tenant *tenant =
		kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Program not found: %s", program);
		__sync_fetch_and_add(&vsc_vmod_kvm->program_notfound, 1);
		return (NULL);
	}

	/* Cannot fail at this point, add to chain */
	if (!kvm_init_chain(ctx, tenant, url, arg)) {
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
	AN(task);

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
		VRT_fail(ctx, "KVM: Program not found: %s", program);
		__sync_fetch_and_add(&vsc_vmod_kvm->program_notfound, 1);
		return (NULL);
	}
	if (!kvm_tenant_debug_allowed(tenant)) {
		VRT_fail(ctx, "KVM: Program not allowed to live-debug: %s", program);
		return (NULL);
	}

	/* Cannot fail at this point, add to chain */
	if (!kvm_init_chain(ctx, tenant, url, arg)) {
		return (NULL);
	}

	init_kvmr(kvmr);
	kvmr->debug = 1;

	init_director(ctx, kvmr);
	return (kvmr->dir);
}

VCL_BOOL vmod_chain(VRT_CTX, VCL_PRIV task,
	VCL_STRING program, VCL_STRING url, VCL_STRING arg, VCL_INT break_status)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

	if (ctx->method != VCL_MET_BACKEND_FETCH) {
		VRT_fail(ctx, "compute: chain() should only be called from vcl_backend_fetch");
		return (0);
	}

	/**
	 * Use automatic non-VM self-request when:
	 * 1. Program is 'fetch'
	 * 2. URL is path-only
	 * NOTE: This is a hidden optimization.
	**/
	if (strcmp(program, "fetch") == 0 && url != NULL && url[0] == '/') {
		if (kvm_init_fetch(ctx, url, arg) == NULL) {
			VRT_fail(ctx,
				"KVM: 'fetch' must be first program, and '%s' should be a self-request", url);
			return (0);
		}
		return (1);
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
	struct kvm_chain_item *item =
		kvm_init_chain(ctx, tenant, url, arg);
	if (item != NULL) {
		item->break_status = break_status;
		return (1);
	}

	return (0);
}
