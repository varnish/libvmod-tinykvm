#include "vmod_riscv.h"

#include "vcl.h"
#include "vcc_if.h"
#include "vmod_util.h"

// Create new machine and call into it.
extern struct vmod_riscv_machine* riscv_create(const char* name,
	const char* file, VRT_CTX, uint64_t insn);
extern void riscv_prewarm(VRT_CTX, struct vmod_riscv_machine*, const char*);
extern int riscv_forkcall(VRT_CTX, struct vmod_riscv_machine*, const char*);
extern int riscv_forkcall_idx(VRT_CTX, struct vmod_riscv_machine*, VCL_INT);
extern int riscv_free(struct vmod_riscv_machine*);
// Adds function to list that will be fed to each new
// machine created afterwards.
extern void riscv_add_known(VRT_CTX, const char* function);
// Functions operating on a machine already forked, which
// is accessible through a priv_task.
extern int riscv_current_call(VRT_CTX, const char*);
extern int riscv_current_call_idx(VRT_CTX, VCL_INT);
extern const char* riscv_current_name(VRT_CTX);
extern const char* riscv_current_result(VRT_CTX);
extern int riscv_current_result_status(VRT_CTX);

/* Create new machine object, which can be used to fork new
   VMs for client requests and backend fetches, etc. */
VCL_VOID
vmod_machine__init(VRT_CTX, struct vmod_riscv_machine **init,
	const char *vcl_name, VCL_STRING name, VCL_STRING elf,
	VCL_INT max_instr, VCL_STRANDS args)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void) vcl_name;

	*init = riscv_create(name, elf, ctx, max_instr);
}
VCL_VOID
vmod_machine__fini(struct vmod_riscv_machine **rvm)
{
	CHECK_OBJ_NOTNULL(*rvm, RISCV_MACHINE_MAGIC);

	riscv_free(*rvm);
	*rvm = NULL;
}

/* Fork and call into a new VM. The VM is freed when the
   request (priv_task) ends. */
VCL_INT vmod_machine_call(VRT_CTX,
	struct vmod_riscv_machine *rvm, VCL_STRING function)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rvm, RISCV_MACHINE_MAGIC);

	return riscv_forkcall(ctx, rvm, function);
}
VCL_INT vmod_machine_call_index(VRT_CTX,
	struct vmod_riscv_machine *rvm, VCL_INT index)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rvm, RISCV_MACHINE_MAGIC);

	return riscv_forkcall_idx(ctx, rvm, index);
}

/* Make given function faster to look up and so on. */
VCL_VOID vmod_machine_add_known_function(VRT_CTX,
	struct vmod_riscv_machine *rvm, VCL_STRING function)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rvm, RISCV_MACHINE_MAGIC);

	return riscv_prewarm(ctx, rvm, function);
}
VCL_VOID vmod_add_known_function(VRT_CTX, VCL_STRING function)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	riscv_add_known(ctx, function);
}

/* Check if there is a VM currently for this request. */
VCL_BOOL vmod_machine_present(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_name(ctx) != NULL;
}
/* Call into any currently running VM. */
VCL_INT vmod_call(VRT_CTX, VCL_STRING func)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_call(ctx, func);
}
VCL_INT vmod_call_index(VRT_CTX, VCL_INT index)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_call_idx(ctx, index);
}
VCL_STRING vmod_current_name(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	return riscv_current_name(ctx);
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
