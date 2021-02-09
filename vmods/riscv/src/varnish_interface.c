#include "vmod_riscv_sandbox.h"

#include <stdbool.h>
#include <string.h>
#include <cache/cache.h>
#include <vsha256.h>

void riscv_SetHash(struct req *req, VSHA256_CTX *ctx)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->is_force_hash = 1;
	VSHA256_Final(req->digest, ctx);
}

void riscv_SetCacheable(VRT_CTX, bool c) {
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->uncacheable && c) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "Ignoring attempt to reset beresp.uncacheable");
	} else {
		ctx->bo->uncacheable = !c;
		//ctx->bo->do_pass = !c;
	}

	const struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f %s",
	    oc->ttl, oc->grace, oc->keep, oc->t_origin,
	    ctx->bo->uncacheable ? "uncacheable" : "cacheable");
}
bool riscv_GetCacheable(VRT_CTX) {
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return !ctx->bo->uncacheable;
}

void riscv_SetTTL(VRT_CTX, float ttl) {
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	oc->ttl = ttl;
}
float riscv_GetTTL(VRT_CTX) {
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	return oc->ttl;
}

long riscv_SetBackend(VRT_CTX, VCL_BACKEND dir)
{
	if (ctx->req != NULL) {
		ctx->req->director_hint = dir;
		return 0;
	} else if (ctx->bo != NULL) {
		ctx->bo->director_req = dir;
		return 0;
	}
	return -1;
}
