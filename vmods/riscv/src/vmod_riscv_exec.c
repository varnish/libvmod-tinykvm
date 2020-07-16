#include "vmod_riscv.h"

#include <malloc.h>
#include "vcl.h"
#include "vcc_if.h"

typedef void (*set_header_t) (struct http*, const char*);
extern const char* execute_riscv(void* workspace, set_header_t, void* http,
	const uint8_t* binary, size_t len, uint64_t instr_max);


VCL_STRING
vmod_exec(VRT_CTX, VCL_HTTP hp, VCL_INT instr_max, VCL_BLOB elf)
{
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	(void) ctx;

	if (elf == NULL)
		return NULL;

	const char* output = execute_riscv(ctx->ws,
		http_SetHeader, hp, elf->priv, elf->len, instr_max);

	return (output); /* leak */
}

VCL_BACKEND vmod_backend_from_body(VRT_CTX, struct vmod_riscv_backend *rvb)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rvb, RISCV_BACKEND_MAGIC);

	return (&rvb->dir);
}

void v_matchproto_(vdi_panic_f)
riscvbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

void v_matchproto_(vdi_finish_f)
riscvbe_finish(const struct director *dir, struct worker *wrk, struct busyobj *bo)
{
	(void) dir;
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
	.name = "riscv_backend",
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

int v_matchproto_(vdi_gethdrs_f)
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

	/* execute the data as ELF */
	const uint8_t *result_data = (const uint8_t *) VSB_data(vsb);
	const size_t   result_len = VSB_len(vsb);

	struct vmod_riscv_backend *rvb;
	CAST_OBJ_NOTNULL(rvb, dir->priv, RISCV_BACKEND_MAGIC);

	struct http *http = bo->beresp;
	if (http == NULL)
		return (-1);

	/* finish the backend request */
	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* simulate some RISC-V */
	const char* output = execute_riscv(
		bo->ws, http_SetHeader, http,
		result_data, result_len, rvb->max_instructions);

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
	if (bo->htc->priv == NULL)
		return (-1);

	/* We need to call this function specifically, otherwise
	   nobody will call our VFP functions */
	typedef void sbe_vfp_init_cb_f(struct busyobj *bo);
	extern void sbe_util_set_vfp_cb(struct busyobj *, sbe_vfp_init_cb_f *);
	sbe_util_set_vfp_cb(bo, vfp_init);
	return (0);
}

VCL_VOID
vmod_backend__init(VRT_CTX, struct vmod_riscv_backend **init,
	const char *vcl_name, VCL_INT max_instr, VCL_STRANDS args)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void) vcl_name;

	struct vmod_riscv_backend *rvb;
	ALLOC_OBJ(rvb, RISCV_BACKEND_MAGIC);
	AN(rvb);

	INIT_OBJ(&rvb->dir, DIRECTOR_MAGIC);
	rvb->dir.priv = rvb;
	rvb->dir.name = "RISC-V director";
	rvb->dir.vcl_name = "TODO: fixme";
	rvb->dir.gethdrs = riscvbe_gethdrs;
	rvb->dir.finish  = riscvbe_finish;
	rvb->dir.panic   = riscvbe_panic;

	rvb->max_instructions = max_instr;

	*init = rvb; /* check this */
}

VCL_VOID
vmod_backend__fini(struct vmod_riscv_backend **priv)
{
	struct vmod_riscv_backend *rvb;

	TAKE_OBJ_NOTNULL(rvb, priv, RISCV_BACKEND_MAGIC);
	FREE_OBJ(rvb);
}
