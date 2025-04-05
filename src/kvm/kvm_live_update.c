#include "vmod_kvm.h"
#include "kvm_live_update.h"

#include <malloc.h>
#include <stdio.h>
#include "vcl.h"
#include "vcc_if.h"

extern struct update_result kvm_live_update(VRT_CTX, struct vmod_kvm_tenant*, const struct update_params*);

static void v_matchproto_(vdi_panic_f)
kvm_updater_be_panic(const struct director *dir, struct vsb *vsb)
{
	(void)dir;
	(void)vsb;
}

#ifdef VARNISH_PLUS
static void v_matchproto_(vdi_finish_f)
kvm_updater_be_finish(const struct director *dir, struct worker *wrk, struct busyobj *bo)
{
	(void) wrk;
	(void) dir;

	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = NULL;
	bo->htc->magic = 0;
	bo->htc = NULL;
}
#else
static void v_matchproto_(vdi_finish_f)
kvm_updater_be_finish(VRT_CTX, const struct director *dir)
{
	(void)dir;

	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct busyobj *bo = ctx->bo;

	if (bo->htc == NULL)
		return;
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	bo->htc->priv = NULL;
	bo->htc->magic = 0;
	bo->htc = NULL;
}
#endif

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
	.name = "kvm_updater",
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

#ifdef VARNISH_PLUS
static int
kvm_updater_aggregate_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct vsb *vsb = (struct vsb *)priv;

	(void)flush;
	(void)last;

	CAST_OBJ_NOTNULL(vsb, priv, VSB_MAGIC);

	VSB_bcat(vsb, ptr, len);
	return (0);
}
#else // open-source
static int
kvm_updater_aggregate_body(void *priv, unsigned flush, const void *ptr, ssize_t len)
{
	struct vsb *vsb = (struct vsb *)priv;

	(void)flush;

	CAST_OBJ_NOTNULL(vsb, priv, VSB_MAGIC);

	VSB_bcat(vsb, ptr, len);
	return (0);
}
#endif

#ifdef VARNISH_PLUS
static int v_matchproto_(vdi_gethdrs_f)
kvm_updater_be_gethdrs(const struct director *dir,
	struct worker *wrk, struct busyobj *bo)
{
	(void)wrk;
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);
#else
static int v_matchproto_(vdi_gethdrs_f)
kvm_updater_be_gethdrs(const struct vrt_ctx *other_ctx, const struct director *dir)
{
	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	struct busyobj *bo = other_ctx->bo;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->bereq, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(bo->beresp, HTTP_MAGIC);
	AZ(bo->htc);
#endif

	/* Put request BODY into vsb */
	struct vsb *vsb = VSB_new_auto();
	AN(vsb);
#ifdef VARNISH_PLUS
	if (bo->req)
		VRB_Iterate(bo->req, kvm_updater_aggregate_body, vsb);
	else if (bo->bereq_body)
		ObjIterate(bo->wrk, bo->bereq_body, vsb, kvm_updater_aggregate_body,
		    0, 0, -1);
#else
	if (bo->req)
		VRB_Iterate(bo->wrk, bo->vsl, bo->req, kvm_updater_aggregate_body, vsb);
	else if (bo->bereq_body)
		ObjIterate(bo->wrk, bo->bereq_body, vsb,
			kvm_updater_aggregate_body, 0);
#endif
	VSB_finish(vsb);

	/* Get the ELF binary */
	const uint8_t *result_data = (uint8_t *) VSB_data(vsb);
	const size_t   result_len = VSB_len(vsb);

	/* Update this machine */
	struct vmod_kvm_updater *kvmu;
	CAST_OBJ_NOTNULL(kvmu, dir->priv, KVM_UPDATER_MAGIC);

	if (result_len > kvmu->max_binary_size && kvmu->max_binary_size != 0)
	{
		http_PutResponse(bo->beresp, "HTTP/1.1", 503, "Binary too large");
		VSB_destroy(&vsb);
		return (-1);
	}

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
#ifndef VARNISH_PLUS
		.vpi = bo->wrk->vpi,
#endif
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

	http_PutResponse(bo->beresp, "HTTP/1.1", 201, NULL);
	http_PrintfHeader(bo->beresp, "Content-Length: %zu", result.len);

	/* store the output in workspace and free result */
	bo->htc->content_length = result.len;
	bo->htc->priv = WS_Copy(bo->ws, result.output, result.len);
	bo->htc->body_status = BS_LENGTH;
#ifndef VARNISH_PLUS
	bo->htc->doclose = SC_REM_CLOSE;
#endif
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

#ifndef VARNISH_PLUS
static const struct vdi_methods live_update_director_methods[1] = {{
	.magic = VDI_METHODS_MAGIC,
	.type  = "kvm_live_update_director_methods",
	.gethdrs = kvm_updater_be_gethdrs,
	.finish  = kvm_updater_be_finish,
}};
#endif

static inline void kvm_update_director(VRT_CTX,
	struct director *dir, struct vmod_kvm_updater *kvmu)
{
	INIT_OBJ(dir, DIRECTOR_MAGIC);
	dir->priv = kvmu;
	dir->vcl_name = "vmod_kvm";
#ifdef VARNISH_PLUS
	dir->name = "KVM live-update director";
	dir->gethdrs = kvm_updater_be_gethdrs;
	dir->finish  = kvm_updater_be_finish;
	dir->panic   = kvm_updater_be_panic;
#else
	struct vcldir *vdir =
		WS_Alloc(ctx->ws, sizeof(struct vcldir));
	if (vdir == NULL) {
		VRT_fail(ctx, "KVM: Out of workspace for internal director");
		return;
	}
	memset(vdir, 0, sizeof(*vdir));
	vdir->magic = VCLDIR_MAGIC;
	vdir->dir = dir;
	vdir->vcl = ctx->vcl;
	vdir->flags |= VDIR_FLG_NOREFCNT;
	vdir->methods = live_update_director_methods;

	dir->vdir = vdir;
#endif
}

static VCL_BACKEND do_live_update(VRT_CTX, VCL_PRIV task,
	VCL_STRING tenant, VCL_STRING key, VCL_BYTES max_size, VCL_BOOL debug)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (key == NULL)
	{
		VRT_fail(ctx, "KVM: Missing key");
		return (NULL);
	}

	struct vmod_kvm_tenant *ten = kvm_tenant_find_key(task, tenant, key);
	if (ten == NULL)
	{
		VRT_fail(ctx, "KVM: Could not find tenant: %s, or wrong key: %s",
				 tenant, key);
		return (NULL);
	}

	if (debug && !kvm_tenant_debug_allowed(ten))
	{
		VRT_fail(ctx, "KVM: Tenant not allowed to live-debug: %s",
				 tenant);
		return (NULL);
	}

	struct vmod_kvm_updater *kvmu;
	kvmu = WS_Alloc(ctx->ws, sizeof(struct vmod_kvm_updater));
	if (kvmu == NULL)
	{
		VRT_fail(ctx, "KVM: Out of workspace for live update");
		return (NULL);
	}

	INIT_OBJ(kvmu, KVM_UPDATER_MAGIC);
	kvmu->max_binary_size = max_size;
	kvmu->tenant   = ten;
	kvmu->is_debug = debug;
	kvm_update_director(ctx, &kvmu->dir, kvmu);

	return (&kvmu->dir);
}

VCL_BACKEND vmod_live_update(VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING key, VCL_BYTES max_size)
{
	return (do_live_update(ctx, task, tenant, key, max_size, 0));
}

VCL_BACKEND vmod_live_debug(
	VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING key, VCL_BYTES max_size)
{
	return (do_live_update(ctx, task, tenant, key, max_size, 1));
}

VCL_BOOL vmod_live_update_file(
	VRT_CTX, VCL_PRIV task, VCL_STRING tenant, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	struct vmod_kvm_tenant *tenptr = kvm_tenant_find(task, tenant);
	if (tenptr == NULL) {
		VRT_fail(ctx, "Could not find tenant: %s", tenant);
		return (0);
	}

	FILE* f = fopen(filename, "rb");
    if (f == NULL) {
		/* NOTE: It is OK to not VRT_fail here as most likely the file
		   access failed or the file does not exist. We return failure. */
		return (0);
	}

    fseek(f, 0, SEEK_END);
	const size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t *)malloc(size);
	if (data == NULL) {
		VRT_fail(ctx, "Could not allocate memory for file: %s", filename);
		fclose(f);
		return (0);
	}

    if (size != fread(data, 1, size, f))
    {
		VRT_fail(ctx, "Could not read file: %s", filename);
		fclose(f);
		free(data);
		return (0);
    }
    fclose(f);

	const struct update_params uparams = {
		.data = data,
		.len  = size,
		.is_debug = 0,
		.debug_port = 0,
	};
	struct update_result result =
		kvm_live_update(ctx, tenptr, &uparams);

	free(data);
	return (result.success);
}
