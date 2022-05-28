#include "vmod_riscv_sandbox.h"

#include <string.h>
#include <cache/cache.h>
#include <vsha256.h>

#include "vcc_if.h"
#include "vmod_util.h"
#include "update_result.h" // vcall_info

extern void init_tenants_str(VRT_CTX, const char*);
extern void init_tenants_file(VRT_CTX, const char*);

extern void* riscv_fork(VRT_CTX, const char* ten, int dbg);
// Functions operating on a machine already forked, which
// is accessible through a priv_task.
extern long riscv_current_call(VRT_CTX, const char*);
extern long riscv_current_call_idx(VRT_CTX, vcall_info);
extern long riscv_current_resume(VRT_CTX);
extern const char* riscv_current_name(VRT_CTX);
extern const char* riscv_current_group(VRT_CTX);
extern const char* riscv_current_result(VRT_CTX);
extern long riscv_current_result_value(VRT_CTX, size_t);
extern const char* riscv_current_result_string(VRT_CTX, size_t);
extern int  riscv_current_is_paused(VRT_CTX);
extern int  riscv_current_apply_hash(VRT_CTX);

#define HDR_INVALID   UINT32_MAX
static inline vcall_info enum_to_idx(VCL_ENUM e)
{
	if (e == vmod_enum_ON_REQUEST) return (vcall_info){1, HDR_REQ, HDR_RESP};
	if (e == vmod_enum_ON_HASH)    return (vcall_info){2, HDR_INVALID, HDR_INVALID};
	if (e == vmod_enum_ON_SYNTH)   return (vcall_info){3, HDR_REQ, HDR_RESP};
	if (e == vmod_enum_ON_BACKEND_FETCH) return (vcall_info){4, HDR_BEREQ, HDR_BERESP};
	if (e == vmod_enum_ON_BACKEND_RESPONSE) return (vcall_info){5, HDR_BEREQ, HDR_BERESP};
	if (e == vmod_enum_ON_DELIVER) return (vcall_info){6, HDR_RESP, HDR_INVALID};
	return (vcall_info){-1, HDR_INVALID, HDR_INVALID};
}

/* Load tenant information from a JSON string */
VCL_VOID vmod_embed_tenants(VRT_CTX, VCL_STRING str)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	init_tenants_str(ctx, str);
}
/* Load tenant information from a JSON file */
VCL_VOID vmod_load_tenants(VRT_CTX, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	init_tenants_file(ctx, filename);
}

/* Fork into a new VM. The VM is freed when the
   request (priv_task) ends. */
VCL_BOOL vmod_fork(VRT_CTX, VCL_STRING tenant, VCL_STRING debug)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_fork(ctx, tenant, debug != NULL) != NULL;
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
	if (function != NULL)
		return riscv_current_call(ctx, function);

	VRT_fail(ctx, "Null was passed to vmod_call");
	return (-1); /* ??? */
}
VCL_INT vmod_vcall(VRT_CTX, VCL_ENUM e)
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

	return riscv_current_result_value(ctx, 0);
}
VCL_INT vmod_result_value(VRT_CTX, VCL_INT idx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_result_value(ctx, idx);
}
VCL_STRING vmod_result_as_string(VRT_CTX, VCL_INT idx)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_result_string(ctx, idx);
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
