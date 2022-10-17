/** 
 * 
*/
#include "vmod_insular.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_if.h"

VCL_BOOL vmod_program(VRT_CTX, VCL_PRIV task, VCL_STRING uri)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_INIT) {
		VRT_fail(ctx, "insular: program() should only be called from vcl_init");
		return (0);
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	return (insular_initial_program(ctx, task, uri));
}

VCL_VOID vmod_on_recv(VRT_CTX, const char *arg)
{
	insular_execute(ctx, 0, arg);
}
