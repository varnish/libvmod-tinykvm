#include "vmod_riscv.h"
#include "riscv_backend.h"

#include <malloc.h>
#include <stdlib.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_if.h"

extern long riscv_current_result_status(VRT_CTX);
extern struct vmod_riscv_machine* riscv_current_machine(VRT_CTX);
extern void riscv_backend_call(VRT_CTX, const void*, long, long, struct backend_result *);

static void v_matchproto_(vdi_panic_f)
riscvbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

static void v_matchproto_(vdi_finish_f)
riscvbe_finish(const struct director *dir, struct worker *wrk, struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	struct vmod_riscv_updater *rvu = (struct vmod_riscv_updater *) dir->priv;
	CHECK_OBJ_NOTNULL(rvu, RISCV_BACKEND_MAGIC);
	//FREE_OBJ(dir);
	//FREE_OBJ(rvu);
	(void) wrk;
	/* */
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
	if (result->content_length == 0) {
		*lp = 0;
		return (VFP_END);
	}

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

static const struct vfp riscv_fetch_processor = {
	.name = "riscv_backend",
	.pull = pull,
};

static void
vfp_init(struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	struct vfp_entry *vfe =
		VFP_Push(bo->vfc, &riscv_fetch_processor);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);

	vfe->priv1 = bo->htc->priv;
	vfe->priv2 = 0;
}

static int v_matchproto_(vdi_gethdrs_f)
riscvbe_gethdrs(const struct director *dir,
	struct worker *wrk, struct busyobj *bo)
{
	(void)wrk;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);

	/* Produce backend response */
	struct vmod_riscv_response *rvr;
	CAST_OBJ_NOTNULL(rvr, dir->priv, RISCV_BACKEND_MAGIC);

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
		VSLb(ctx.vsl, SLT_Error, "Backend VM: Out of workspace for result");
		return (-1);
	}
	result->bufcount = VMBE_NUM_BUFFERS;

	riscv_backend_call(&ctx, rvr->priv_key, rvr->funcaddr, rvr->funcarg, result);

	/* Status code is sanitized in the backend call */
	http_PutResponse(bo->beresp, "HTTP/1.1", result->status, NULL);
	/* Allow missing content-type when no content present */
	if (result->content_length > 0)
	{
		http_PrintfHeader(bo->beresp, "Content-Type: %.*s",
			(int) result->tsize, result->type);
		http_PrintfHeader(bo->beresp,
			"Content-Length: %zu", result->content_length);
	}

	char timestamp[VTIM_FORMAT_SIZE];
	VTIM_format(VTIM_real(), timestamp);
	http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* store the output in workspace and free result */
	bo->htc->content_length = result->content_length;
	bo->htc->priv = (void *)result;
	bo->htc->body_status = BS_LENGTH;

	vfp_init(bo);
	return (0);
}

static void setup_backend_director(struct director *dir, struct vmod_riscv_response *rvr)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = rvr;
	dir->name = "VM backend director";
	dir->vcl_name = "vmod_riscv";
	dir->gethdrs = riscvbe_gethdrs;
	dir->finish  = riscvbe_finish;
	dir->panic   = riscvbe_panic;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_STRING func, VCL_STRING farg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_response *rvr;
	rvr = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_response));
	if (rvr == NULL) {
		VRT_fail(ctx, "Out of workspace");
		return NULL;
	}

	INIT_OBJ(rvr, RISCV_BACKEND_MAGIC);
	rvr->priv_key = ctx->bo;
	rvr->machine = riscv_current_machine(ctx);
	if (rvr->machine == NULL) {
		VRT_fail(ctx, "VM backend: No active tenant");
		return NULL;
	}

	if (func) {
		rvr->funcaddr = atoi(func);
		rvr->funcarg  = atoi(farg);
		/* TODO: If it's null, should we abandon? */
		if (rvr->funcaddr == 0x0) {
			return (NULL);
		}
	}
	rvr->max_response_size = 0;

	setup_backend_director(&rvr->dir, rvr);

	return &rvr->dir;
}
