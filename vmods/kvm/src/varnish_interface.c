#include <stdbool.h>
#include <string.h>
#include <cache/cache.h>
#include <vsb.h>
#include <vcl.h>
#include <vsha256.h>

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
