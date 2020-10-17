#include "vmod_riscv_sandbox.h"

#include <string.h>
#include <cache/cache.h>
#include <vsha256.h>

#include "vcc_if.h"
#include "vmod_util.h"

extern void init_tenants(VRT_CTX, const char*);

extern void* riscv_fork(VRT_CTX, const char*);
// Functions operating on a machine already forked, which
// is accessible through a priv_task.
extern long riscv_current_call(VRT_CTX, const char*);
extern long riscv_current_call_idx(VRT_CTX, int);
extern long riscv_current_resume(VRT_CTX);
extern const char* riscv_current_name(VRT_CTX);
extern const char* riscv_current_group(VRT_CTX);
extern const char* riscv_current_result(VRT_CTX);
extern long riscv_current_result_status(VRT_CTX);
extern int  riscv_current_is_paused(VRT_CTX);
extern int  riscv_current_apply_hash(VRT_CTX);

static inline int enum_to_idx(VCL_ENUM e)
{
	if (e == vmod_enum_ON_REQUEST) return 1;
	if (e == vmod_enum_ON_HASH)    return 2;
	if (e == vmod_enum_ON_SYNTH)   return 3;
	if (e == vmod_enum_ON_BACKEND_FETCH) return 4;
	if (e == vmod_enum_ON_BACKEND_RESPONSE) return 5;
	return -1;
}

/* Load tenant information from a JSON file */
VCL_VOID vmod_load_tenants(VRT_CTX, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	init_tenants(ctx, filename);
}

/* Fork into a new VM. The VM is freed when the
   request (priv_task) ends. */
VCL_BOOL vmod_fork(VRT_CTX, VCL_STRING tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_fork(ctx, tenant) != NULL;
}

/* Check if there is a VM currently for this request. */
VCL_BOOL vmod_active(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_name(ctx) != NULL;
}
/* Call into any currently running VM. */
VCL_INT vmod_call(VRT_CTX, VCL_STRING function)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (function == NULL)
		return (-1); /* ??? */

	return riscv_current_call(ctx, function);
}
VCL_INT vmod_fastcall(VRT_CTX, VCL_ENUM e)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_call_idx(ctx, enum_to_idx(e));
}
VCL_INT vmod_resume(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_resume(ctx);
}
VCL_STRING vmod_current_name(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_name(ctx);
}
VCL_STRING vmod_current_group(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_group(ctx);
}

/* Returns a string that represents what the VM wants to happen next.
   Such as: "lookup", "synth", ... */
VCL_STRING vmod_want_result(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_result(ctx);
}
/* Returns the status code the VM wants to return, when relevant.
   Such as when calling synth(). */
VCL_INT vmod_want_status(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_result_status(ctx);
}
VCL_BOOL vmod_want_resume(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_is_paused(ctx);
}
VCL_BOOL vmod_apply_hash(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_apply_hash(ctx);
}
