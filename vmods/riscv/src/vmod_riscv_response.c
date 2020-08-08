#include "vmod_riscv.h"

#include <malloc.h>
#include "vcl.h"
#include "vcc_if.h"
#include "vmod_util.h"

extern struct vmod_riscv_machine* riscv_current_machine(VRT_CTX);
extern struct backend_buffer riscv_backend_call(VRT_CTX, struct vmod_riscv_machine*, const char*);

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

	ssize_t len = (vfe->priv2 > *lp) ? *lp : vfe->priv2;
	assert(len == 0 || vfe->priv1);
	memcpy(p, vfe->priv1, len);
	*lp = len;
	vfe->priv1 = (char *)vfe->priv1 + len;
	vfe->priv2 -= len;
	if (vfe->priv2)
		return (VFP_OK);
	else
		return (VFP_END);
}

static const struct vfp riscv_fetch_processor = {
	.name = "riscv_response_backend",
	.pull = pull,
};

static void v_matchproto_(sbe_vfp_init_cb_f)
vfp_init(struct busyobj *bo)
{
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	struct vfp_entry *vfe =
		VFP_Push(bo->vfc, &riscv_fetch_processor);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);

	vfe->priv1 = bo->htc->priv;
	vfe->priv2 = bo->htc->content_length;
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

	/* finish the backend request */
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* Produce backend response */
	struct vmod_riscv_response *rvr;
	CAST_OBJ_NOTNULL(rvr, dir->priv, RISCV_BACKEND_MAGIC);

	const struct vrt_ctx ctx = {
		.ws = bo->ws,
		.vsl = bo->vsl,
		.http_bereq = bo->bereq,
		.http_beresp = bo->beresp,
	};
	struct backend_buffer output =
		riscv_backend_call(&ctx, rvr->machine, rvr->func);

	if (output.data == NULL || output.type == NULL)
	{
		http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
		return (-1);
	}
	else
	{
		http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
		http_PrintfHeader(bo->beresp, "Content-Length: %u", output.size);
		http_PrintfHeader(bo->beresp, "Content-Type: %s", output.type);

		/* store the output in workspace and free result */
		bo->htc->content_length = output.size;
		bo->htc->priv = WS_Copy(bo->ws, output.data, output.size);
		bo->htc->body_status = BS_LENGTH;

		free((void*) output.data);
		free((void*) output.type);

		if (bo->htc->priv == NULL)
			return (-1);
	}

	/* We need to call this function specifically, otherwise
	   nobody will call our VFP functions */
	typedef void sbe_vfp_init_cb_f(struct busyobj *bo);
	extern void sbe_util_set_vfp_cb(struct busyobj *, sbe_vfp_init_cb_f *);
	sbe_util_set_vfp_cb(bo, vfp_init);
	return (0);
}

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_STRING func)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_response *rvr;
	rvr = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_response));
	if (rvr == NULL) {
		VRT_fail(ctx, "Out of memory");
		return NULL;
	}

	INIT_OBJ(rvr, RISCV_BACKEND_MAGIC);
	rvr->machine = riscv_current_machine(ctx);
	rvr->func = func;
	rvr->max_response_size = 0;

	INIT_OBJ(&rvr->dir, DIRECTOR_MAGIC);
	rvr->dir.priv = rvr;
	rvr->dir.name = "VM response director";
	rvr->dir.vcl_name = "vmod_vm_backend";
	rvr->dir.gethdrs = riscvbe_gethdrs;
	rvr->dir.finish  = riscvbe_finish;
	rvr->dir.panic   = riscvbe_panic;

	return &rvr->dir;
}
