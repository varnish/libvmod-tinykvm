#include "vmod_kvm.h"
#include "kvm_live_update.h"

#include <malloc.h>
#include "vcl.h"
#include "vcc_if.h"

extern struct update_result kvm_live_update(VRT_CTX, struct vmod_kvm_tenant*, const struct update_params*);

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

static const struct vfp kvm_fetch_processor = {
	.name = "kvm_updater_backend",
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
kvmbe_gethdrs(const struct director *dir,
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
	struct vmod_kvm_updater *kvmu;
	CAST_OBJ_NOTNULL(kvmu, dir->priv, KVM_BACKEND_MAGIC);

	if (result_len <= kvmu->max_binary_size)
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
			.is_debug = kvmu->is_debug,
			.debug_port = kvmu->debug_port,
		};
		struct update_result result =
			kvm_live_update(&ctx, kvmu->tenant, &uparams);

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

static inline void kvmu_director(
	struct director *dir, struct vmod_kvm_updater *kvmu)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmu;
	dir->name = "VM updater director";
	dir->vcl_name = "vmod_machine_update";
	dir->gethdrs = kvmbe_gethdrs;
	dir->finish  = kvmbe_finish;
	dir->panic   = kvmbe_panic;
}

VCL_BACKEND vmod_live_update(VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING key, VCL_BYTES max_size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (key == NULL) {
		VRT_fail(ctx, "Missing key");
		return NULL;
	}

	struct vmod_kvm_tenant *ten = kvm_tenant_find_key(task, tenant, key);
	if (ten == NULL) {
		VRT_fail(ctx, "Could not find tenant: %s, or wrong key: %s",
			tenant, key);
		return NULL;
	}

	struct vmod_kvm_updater *kvmu;
	kvmu = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_updater));
	if (kvmu == NULL) {
		VRT_fail(ctx, "Out of workspace for live update");
		return NULL;
	}

	INIT_OBJ(kvmu, KVM_BACKEND_MAGIC);
	kvmu->max_binary_size = max_size;
	kvmu->tenant = ten;
	kvmu->is_debug = 0;
	kvmu_director(&kvmu->dir, kvmu);

	return &kvmu->dir;
}

VCL_BACKEND vmod_live_debug(
	VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING key, VCL_BYTES max_size)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (key == NULL) {
		VRT_fail(ctx, "Missing key");
		return NULL;
	}

	struct vmod_kvm_tenant *ten = kvm_tenant_find_key(task, tenant, key);
	if (ten == NULL) {
		VRT_fail(ctx, "Could not find tenant: %s, or wrong key: %s",
			tenant, key);
		return NULL;
	}

	struct vmod_kvm_updater *kvmu;
	kvmu = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_updater));
	if (kvmu == NULL) {
		VRT_fail(ctx, "Out of workspace for live debugging");
		return NULL;
	}

	INIT_OBJ(kvmu, KVM_BACKEND_MAGIC);
	kvmu->max_binary_size = max_size;
	kvmu->tenant = ten;
	kvmu->is_debug   = 1;
	kvmu->debug_port = 0;
	kvmu_director(&kvmu->dir, kvmu);

	return &kvmu->dir;
}
