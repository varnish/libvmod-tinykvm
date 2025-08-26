#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <cache/cache.h>
#include <vsb.h>
#include <vcl.h>
#include <vsha256.h>

struct ws *kvm_CreateWorkspace(const char *id, unsigned size)
{
	void *mem = malloc(size);
	if (mem == NULL)
		return NULL;
	struct ws *ws = malloc(sizeof(*ws));
	if (ws == NULL) {
		free(mem);
		return NULL;
	}
	WS_Init(ws, id, mem, size);
	return ws;
}
void kvm_FreeWorkspace(struct ws *ws)
{
	if (ws == NULL)
		return;
	free(ws->s);
	free(ws);
}

long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir)
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

void kvm_SetCacheable(VRT_CTX, bool c)
{
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	if (ctx->bo->uncacheable && c) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		    "Ignoring attempt to reset beresp.uncacheable");
	} else {
		ctx->bo->uncacheable = !c;
	}

	const struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	VSLb(ctx->vsl, SLT_TTL, "VCL %.0f %.0f %.0f %.0f %s",
	    oc->ttl, oc->grace, oc->keep, oc->t_origin,
	    ctx->bo->uncacheable ? "uncacheable" : "cacheable");
}
bool kvm_GetCacheable(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	return !ctx->bo->uncacheable;
}

void kvm_SetTTLs(VRT_CTX, float ttl, float grace, float keep)
{
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	oc->ttl = ttl;
	oc->grace = grace;
	oc->keep = keep;
}
float kvm_GetTTL(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);
	struct objcore *oc = ctx->bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	return oc->ttl;
}
