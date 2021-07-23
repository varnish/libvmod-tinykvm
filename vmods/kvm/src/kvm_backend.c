#include "vmod_kvm.h"

#include <malloc.h>
#include <stdlib.h>
#include <vtim.h>
#include "vcl.h"
#include "vcc_if.h"

extern long kvm_current_result_status(VRT_CTX);
extern struct vmod_kvm_machine *kvm_get_machine(VRT_CTX, VCL_STRING);
extern struct backend_buffer kvm_backend_call(VRT_CTX, const void *, long, long);
extern uint64_t kvm_resolve_name(struct vmod_kvm_machine *, const char*);

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
	//FREE_OBJ(dir);
	//FREE_OBJ(kvmu);
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
	vfe->priv2 = bo->htc->content_length;
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
	struct backend_buffer output =
		kvm_backend_call(&ctx, kvmr->priv_key, kvmr->funcaddr, kvmr->funcarg);

	if (output.data == NULL || output.type == NULL)
	{
		http_PutResponse(bo->beresp, "HTTP/1.1", 503, NULL);
		return (-1);
	}

	http_PutResponse(bo->beresp, "HTTP/1.1", 200, NULL);
	http_PrintfHeader(bo->beresp, "Content-Type: %.*s",
		(int) output.tsize, output.type);
	http_PrintfHeader(bo->beresp, "Content-Length: %zu", output.size);

	char timestamp[VTIM_FORMAT_SIZE];
	VTIM_format(VTIM_real(), timestamp);
	http_PrintfHeader(bo->beresp, "Last-Modified: %s", timestamp);

	bo->htc = WS_Alloc(bo->ws, sizeof *bo->htc);
	if (bo->htc == NULL)
		return (-1);
	INIT_OBJ(bo->htc, HTTP_CONN_MAGIC);

	/* store the output in workspace and free result */
	bo->htc->content_length = output.size;
	bo->htc->priv = (void *)output.data;
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

VCL_BACKEND vmod_vm_backend(VRT_CTX, VCL_STRING tenant, VCL_STRING func, VCL_STRING farg)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_kvm_response *kvmr;
	kvmr = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_response));
	if (kvmr == NULL) {
		VRT_fail(ctx, "Out of workspace");
		return NULL;
	}

	INIT_OBJ(kvmr, KVM_BACKEND_MAGIC);
	kvmr->priv_key = ctx;
	kvmr->machine = kvm_get_machine(ctx, tenant);
	if (kvmr->machine == NULL) {
		VRT_fail(ctx, "VM backend: No active tenant");
		return NULL;
	}

	if (func) {
		kvmr->funcaddr = atoi(func);
		kvmr->funcarg  = atoi(farg);
		/* If it's not an address, lookup as a public function */
		if (kvmr->funcaddr == 0x0) {
			kvmr->funcaddr = kvm_resolve_name(kvmr->machine, func);
		}
	}
	kvmr->max_response_size = 0;

	setup_response_director(&kvmr->dir, kvmr);

	return &kvmr->dir;
}
