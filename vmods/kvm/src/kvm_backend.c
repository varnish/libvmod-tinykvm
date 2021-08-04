#include "vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_if.h"
extern void kvm_backend_call(VRT_CTX, struct vmod_kvm_tenant *, const char *, const char *, struct backend_result *);

static void v_matchproto_(vdi_panic_f)
kvmbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

static void v_matchproto_(vdi_finish_f)
kvmbe_finish(const struct director *dir, struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	struct vmod_kvm_updater *kvmu = (struct vmod_kvm_updater *) dir->priv;
	CHECK_OBJ_NOTNULL(kvmu, KVM_BACKEND_MAGIC);
	(void) wrk;
	/* The objects here are workspace allocated */
	bo->htc = NULL;
}

static enum vfp_status v_matchproto_(vfp_pull_f)
pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	AN(p);
	AN(lp);

	struct backend_result *result = (struct backend_result *)vfe->priv1;
	struct VMBuffer *current = &result->buffers[vfe->priv2];

	ssize_t written = 0;
	while (1) {
		ssize_t len = (current->size > *lp) ? *lp : current->size;
		memcpy(p, current->data, len);
		p = (void *) ((char *)p + len);
		written += len;
		current->data += len;
		current->size -= len;
		/* Return later if there's more, but we can't send more */
		if (len == 0 && current->size > 0) {
			assert(vfe->priv2 < (ssize_t)result->bufcount);
			*lp = written;
			return (VFP_OK);
		}
		/* Go to next buffer */
		vfe->priv2 ++;
		/* Reaching bufcount means end of fetch */
		if (vfe->priv2 == (ssize_t)result->bufcount) {
			assert(current->size == 0);
			*lp = written;
			return VFP_END;
		}
		current = &result->buffers[vfe->priv2];
	}
}

static const struct vfp kvm_fetch_processor = {
	.name = "kvm_response_backend",
	.pull = pull,
};

static void
vfp_init(struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	struct vfp_entry *vfe =
		VFP_Push(bo->vfc, &kvm_fetch_processor);
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

	/* Produce backend response */
	struct vmod_kvm_response *kvmr;
	CAST_OBJ_NOTNULL(kvmr, dir->priv, KVM_BACKEND_MAGIC);

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

	struct backend_result *result =
		(struct backend_result *)WS_Alloc(bo->ws, VMBE_RESULT_SIZE);
	if (result == NULL) {
		VSLb(ctx.vsl, SLT_Error, "Backend VM: Out of workspace\n");
		return (-1);
	}

	result->bufcount = VMBE_NUM_BUFFERS;
	kvm_backend_call(&ctx, kvmr->tenant, kvmr->func, kvmr->funcarg, result);

	/* XXX: Content-Length: 0 is probably allowed */
	if (result->tsize == 0 || result->content_length == 0)
	{
		http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
		return (-1);
	}

	http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
	http_PrintfHeader(bo->beresp, "Content-Type: %.*s",
		(int) result->tsize, result->type);
	http_PrintfHeader(bo->beresp, "Content-Length: %zu", result->content_length);

	char timestamp[VTIM_FORMAT_SIZE];
	VTIM_format(VTIM_real(), timestamp);
	http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* store the result in workspace and free result */
	bo->htc->content_length = result->content_length;
	bo->htc->priv = (void *)result;
	bo->htc->body_status = BS_LENGTH;

	vfp_init(bo);
	return (0);
}

static void setup_response_director(struct director *dir, struct vmod_kvm_response *kvmr)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmr;
	dir->name = "KVM backend director";
	dir->vcl_name = "vmod_kvm";
	dir->gethdrs = kvmbe_gethdrs;
	dir->finish  = kvmbe_finish;
	dir->panic   = kvmbe_panic;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING func, VCL_STRING farg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_kvm_response *kvmr;
	kvmr = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_response));
	if (kvmr == NULL) {
		VRT_fail(ctx, "Out of workspace");
		return (NULL);
	}

	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	kvmr->priv_key = ctx;
	kvmr->tenant = kvm_tenant_find(task, tenant);
	if (kvmr->tenant == NULL) {
		VRT_fail(ctx, "KVM sandbox says 'No such tenant': %s", tenant);
		return (NULL);
	}

	kvmr->func = func;
	kvmr->funcarg = farg;
	kvmr->max_response_size = 0;

	setup_response_director(&kvmr->dir, kvmr);

	return &kvmr->dir;
}
