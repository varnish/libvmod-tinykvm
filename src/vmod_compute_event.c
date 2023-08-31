#include "config.h"

#include "vdef.h"
#include "vrt.h"
#include "vcl.h"
#include "vas.h"
#include "miniobj.h"

#include "vcc_compute_if.h"
extern void handle_vmod_event(struct vcl *vcl, enum vcl_event_e e);

int v_matchproto_(vmod_event_f)
	vmod_event(VRT_CTX, struct vmod_priv *vp, enum vcl_event_e e)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	(void) vp;

#ifdef VMOD_EVENT_ENABLED
	handle_vmod_event(ctx->vcl, e);
#else
	(void)ctx; (void)e;
#endif
	// NOTE: Tenancy structure is automatically freed.
	return (0);
}
