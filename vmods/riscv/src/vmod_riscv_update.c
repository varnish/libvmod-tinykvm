#include "vmod_riscv.h"

#include <malloc.h>
#include "vcl.h"
#include "vcc_if.h"
#include "vmod_util.h"

extern const char* riscv_update(struct vmod_riscv_machine*, const uint8_t*, size_t);


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
	.name = "riscv_updater_backend",
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


static int
aggregate_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct vsb *vsb = (struct vsb *)priv;

	(void)flush;
	(void)last;

	CAST_OBJ_NOTNULL(vsb, priv, VSB_MAGIC);

	VSB_bcat(vsb, ptr, len);
	return (0);
}

static void v_matchproto_(vmod_priv_free_f)
destroy_vsb(void *priv)
{
	struct vsb *vsb;

	AN(priv);
	CAST_OBJ_NOTNULL(vsb, priv, VSB_MAGIC);

	VSB_destroy(&vsb);
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

	/* Put request BODY into vsb */
	struct vsb *vsb = VSB_new_auto();
	AN(vsb);
	if (bo->req)
		VRB_Iterate(bo->req, aggregate_body, vsb);
	else if (bo->bereq_body)
		ObjIterate(bo->wrk, bo->bereq_body, vsb, aggregate_body,
		    0, 0, -1);
	VSB_finish(vsb);

	/* cleanup for vsb */
	struct vmod_priv *priv = vmod_util_get_priv_task(NULL, bo, dir);
	priv->priv = vsb;
	priv->free = destroy_vsb;

	/* Get the ELF binary */
	const uint8_t *result_data = (const uint8_t *) VSB_data(vsb);
	const size_t   result_len = VSB_len(vsb);

	/* finish the backend request */
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* Update this machine */
	struct vmod_riscv_updater *rvu;
	CAST_OBJ_NOTNULL(rvu, dir->priv, RISCV_BACKEND_MAGIC);

	if (result_len <= rvu->max_binary_size)
	{
		const char* output = riscv_update(rvu->machine, result_data, result_len);

		const size_t output_len = __builtin_strlen(output);
		http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
		http_PrintfHeader(bo->beresp, "Content-Length: %jd", output_len);

		/* store the output in workspace and free result */
		bo->htc->content_length = output_len;
		bo->htc->priv = WS_Copy(bo->ws, output, output_len);
		bo->htc->body_status = BS_LENGTH;
		/* The zero-length string that can be returned is .rodata */
		if (output != NULL && output[0] != 0)
			free((void*) output);
	}
	else {
		http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
	}
	if (bo->htc->priv == NULL)
		return (-1);

	/* We need to call this function specifically, otherwise
	   nobody will call our VFP functions */
	typedef void sbe_vfp_init_cb_f(struct busyobj *bo);
	extern void sbe_util_set_vfp_cb(struct busyobj *, sbe_vfp_init_cb_f *);
	sbe_util_set_vfp_cb(bo, vfp_init);
	return (0);
}

VCL_BACKEND vmod_machine_live_update(VRT_CTX, struct vmod_riscv_machine *rvm, VCL_BYTES max_size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_updater *rvu;
	rvu = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_updater));
	if (rvu == NULL) {
		VRT_fail(ctx, "Unable to allocate memory for update");
		return NULL;
	}

	INIT_OBJ(rvu, RISCV_BACKEND_MAGIC);
	rvu->max_binary_size = max_size;
	rvu->machine = rvm;

	INIT_OBJ(&rvu->dir, DIRECTOR_MAGIC);
	rvu->dir.priv = rvu;
	rvu->dir.name = "VM updater director";
	rvu->dir.vcl_name = "vmod_machine_update";
	rvu->dir.gethdrs = riscvbe_gethdrs;
	rvu->dir.finish  = riscvbe_finish;
	rvu->dir.panic   = riscvbe_panic;

	return &rvu->dir;
}
