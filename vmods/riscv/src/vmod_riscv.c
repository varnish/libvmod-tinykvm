#include "vmod_riscv.h"

#include <malloc.h>
#include "vcl.h"

#include "vcc_if.h"

typedef void (*set_header_t) (struct http*, const char*);
extern const char*
execute(set_header_t, void*, const char* binary, size_t len, uint64_t instr_max);
#include "vmod_util.h"

VCL_STRING
vmod_exec(VRT_CTX, VCL_HTTP hp, VCL_INT instr_max, VCL_STRING elf)
{
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	(void) ctx;

	if (elf == NULL)
		return NULL;
	const size_t len = __builtin_strlen(elf);

	const char* output = execute(
		http_SetHeader, hp, elf, len, instr_max);

	return output; /* leak */
}

void v_matchproto_(vdi_panic_f)
riscvbe_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

void v_matchproto_(vdi_finish_f)
riscvbe_finish(const struct director *dir, struct worker *, struct busyobj *bo)
{
	(void)dir;
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

	ssize_t len = vfe->priv2;
	assert(len == 0 || vfe->priv1);
	if (len > *lp)
		len = *lp;
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



int v_matchproto_(vdi_gethdrs_f)
riscvbe_gethdrs(const struct director *dir,
	struct worker *wrk, struct busyobj *bo)
{
	struct vmod_priv *priv = vmod_util_get_priv_task(NULL, bo, dir);
	AN(priv->priv);

	const char *result_string = priv->priv;
	const size_t result_len = __builtin_strlen(result_string);

	(void)wrk;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);

	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	bo->htc->priv = (void*) result_string;
	bo->htc->content_length = result_len;
	bo->htc->body_status = BS_LENGTH;

	http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
	http_PrintfHeader(bo->beresp, "Content-Length: %jd", result_len);

	static int id;
	struct vmod_priv *betask = vmod_util_get_priv_task(NULL, bo, &id);
	betask->priv = vfp_init;
	return (0);
}

VCL_BACKEND vmod_init_backend(VRT_CTX, struct vmod_riscv_init *rvb)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rvb, RISCV_BACKEND_MAGIC);

	return &rvb->dir;
}

VCL_VOID
vmod_init__init(VRT_CTX, struct vmod_riscv_init **init,
	const char *, VCL_INT max_instr, VCL_STRANDS args)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void) ctx;

	struct vmod_riscv_init *rvb;
	ALLOC_OBJ(rvb, RISCV_BACKEND_MAGIC);
	AN(rvb);

	INIT_OBJ(&rvb->dir, DIRECTOR_MAGIC);
	rvb->dir.priv = rvb;
	rvb->dir.name = "RISC-V director";
	rvb->dir.vcl_name = "TODO: fixme";
	rvb->dir.gethdrs = riscvbe_gethdrs;
	rvb->dir.finish  = riscvbe_finish;
	rvb->dir.panic   = riscvbe_panic;

	/* TODO: initialize max_instructions */
	rvb->max_instructions = max_instr;

	*init = rvb; /* check this */
}

VCL_VOID
vmod_init__fini(struct vmod_riscv_init **priv)
{
	struct vmod_riscv_init *rvb;

	TAKE_OBJ_NOTNULL(rvb, priv, RISCV_BACKEND_MAGIC);
	FREE_OBJ(rvb);
}
