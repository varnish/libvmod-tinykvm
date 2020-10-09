#pragma once

extern "C" {
# include <vdef.h>
# include <vre.h>
# include <vrt.h>
# include <vsb.h>
# include <vcl.h>
# include <vapi/vsl_int.h>
# include <vsha256.h>
	void *WS_Alloc(struct ws *ws, unsigned bytes);
	void *WS_Copy(struct ws *ws, const void *str, int len);
	char *WS_Printf(struct ws *ws, const char *fmt, ...);
	void VSL(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, ...);
	void VSLb(struct vsl_log *, int tag, const char *fmt, ...);
}
