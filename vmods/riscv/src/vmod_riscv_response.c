#include "vmod_riscv.h"

#include <malloc.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_if.h"
#include "vmod_util.h"

extern long riscv_current_result_status(VRT_CTX);
extern struct vmod_riscv_machine* riscv_current_machine(VRT_CTX);
extern struct backend_buffer riscv_backend_call(VRT_CTX, struct vmod_riscv_machine*, long);
extern uint64_t riscv_resolve_name(struct vmod_riscv_machine*, const char*);

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
		riscv_backend_call(&ctx, rvr->machine, rvr->funcaddr);

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

		char timestamp[VTIM_FORMAT_SIZE];
		VTIM_format(VTIM_real(), timestamp);
		http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

		/* store the output in workspace and free result */
		bo->htc->content_length = output.size;
		bo->htc->priv = (void *)output.data;
		bo->htc->body_status = BS_LENGTH;

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

static void setup_response_director(struct director *dir, struct vmod_riscv_response *rvr)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = rvr;
	dir->name = "VM backend director";
	dir->vcl_name = "vmod_riscv";
	dir->gethdrs = riscvbe_gethdrs;
	dir->finish  = riscvbe_finish;
	dir->panic   = riscvbe_panic;
}

VCL_BACKEND vmod_vm_backend(VRT_CTX)
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
	rvr->funcaddr = riscv_current_result_status(ctx);
	rvr->max_response_size = 0;

	setup_response_director(&rvr->dir, rvr);

	return &rvr->dir;
}

VCL_BACKEND vmod_machine_vm_backend(VRT_CTX,
    struct vmod_riscv_machine *machine, VCL_STRING func)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_riscv_response *rvr;
	rvr = WS_Alloc(ctx->ws, sizeof(struct vmod_riscv_response));
	if (rvr == NULL) {
		VRT_fail(ctx, "Out of memory");
		return NULL;
	}

	INIT_OBJ(rvr, RISCV_BACKEND_MAGIC);
	rvr->machine = machine;
	rvr->funcaddr = riscv_resolve_name(machine, func);
	rvr->max_response_size = 0;

	setup_response_director(&rvr->dir, rvr);

	return &rvr->dir;
}
