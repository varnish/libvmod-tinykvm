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

void riscv_SetCacheable(VRT_CTX, bool a) {
	ctx->bo->do_pass = a;
}
bool riscv_GetCacheable(VRT_CTX) {
	return ctx->bo->do_pass;
}

void riscv_SetTTL(VRT_CTX, float ttl) {
	struct objcore *oc = ctx->bo->fetch_objcore;
	oc->ttl = ttl;
}
float riscv_GetTTL(VRT_CTX) {
	struct objcore *oc = ctx->bo->fetch_objcore;
	return oc->ttl;
}
