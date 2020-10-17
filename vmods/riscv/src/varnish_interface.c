#include "vmod_riscv_sandbox.h"

#include <string.h>
#include <cache/cache.h>
#include <vsha256.h>

void riscv_SetHash(struct req *req, VSHA256_CTX *ctx)
{
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	req->is_force_hash = 1;
	VSHA256_Final(req->digest, ctx);
}
