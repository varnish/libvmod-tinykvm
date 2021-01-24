#include "vmod_riscv.h"
#include "vmod_riscv_sandbox.h"
#include "update_result.h"

#include <malloc.h>
#include "vcl.h"
#include "vcc_if.h"

extern struct update_result riscv_update(VRT_CTX, struct vmod_riscv_machine*, const struct update_params*);
extern const struct vmod_riscv_machine* riscv_current_machine(VRT_CTX);
extern struct vmod_riscv_machine* tenant_find(VRT_CTX, const char*);

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

static void
vfp_init(struct busyobj *bo)
{
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

	/* Get the ELF binary */
	const uint8_t *result_data = (uint8_t *) VSB_data(vsb);
	const size_t   result_len = VSB_len(vsb);

	/* Update this machine */
	struct vmod_riscv_updater *rvu;
	CAST_OBJ_NOTNULL(rvu, dir->priv, RISCV_BACKEND_MAGIC);

	if (result_len <= rvu->max_binary_size)
	{
		/* finish the backend request */
		bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
		if (bo->htc == NULL) {
			VSB_destroy(&vsb);
			return (-1);
		}
		INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

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
		const struct update_params uparams = {
			.data = result_data,
			.len  = result_len,
			.is_debug = rvu->is_debug,
		};
		struct update_result result =
			riscv_update(&ctx, rvu->machine, &uparams);

		http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
		http_PrintfHeader(bo->beresp, "Content-Length: %zu", result.len);

		/* store the output in workspace and free result */
		bo->htc->content_length = result.len;
		bo->htc->priv = WS_Copy(bo->ws, result.output, result.len);
		bo->htc->body_status = BS_LENGTH;
		/* Delete the result */
		if (result.destructor)
			result.destructor(&result);

		if (bo->htc->priv == NULL) {
			http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
			VSB_destroy(&vsb);
			return (-1);
		}

		vfp_init(bo);
		VSB_destroy(&vsb);
		return (0);
	}

	http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
	VSB_destroy(&vsb);
	return (-1);
}

static inline void rvu_director(
	struct director *dir, struct vmod_riscv_updater *rvu)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = rvu;
	dir->name = "VM updater director";
	dir->vcl_name = "vmod_machine_update";
	dir->gethdrs = riscvbe_gethdrs;
	dir->finish  = riscvbe_finish;
	dir->panic   = riscvbe_panic;
}

VCL_BACKEND vmod_live_update(VRT_CTX, VCL_STRING tenant, VCL_BYTES max_size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_machine *rvm = tenant_find(ctx, tenant);
	if (rvm == NULL) {
		VRT_fail(ctx, "Could not find tenant: %s", tenant);
		return NULL;
	}

	struct vmod_riscv_updater *rvu;
	rvu = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_updater));
	if (rvu == NULL) {
		VRT_fail(ctx, "Unable to allocate memory for update");
		return NULL;
	}

	INIT_OBJ(rvu, RISCV_BACKEND_MAGIC);
	rvu->max_binary_size = max_size;
	rvu->machine = rvm;
	rvu->is_debug = 0;
	rvu_director(&rvu->dir, rvu);

	return &rvu->dir;
}

VCL_BACKEND vmod_live_debug(
	VRT_CTX, VCL_STRING tenant, VCL_BYTES max_size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_machine *rvm = tenant_find(ctx, tenant);
	if (rvm == NULL) {
		VRT_fail(ctx, "Could not find tenant: %s", tenant);
		return NULL;
	}

	struct vmod_riscv_updater *rvu;
	rvu = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_updater));
	if (rvu == NULL) {
		VRT_fail(ctx, "Unable to allocate memory for update");
		return NULL;
	}

	INIT_OBJ(rvu, RISCV_BACKEND_MAGIC);
	rvu->max_binary_size = max_size;
	rvu->machine = rvm;
	rvu->is_debug   = 1;
	rvu->debug_port = 0;
	rvu_director(&rvu->dir, rvu);

	return &rvu->dir;
}
