/*-
 * Copyright (c) 2019 Varnish Software AS
 * All rights reserved.
 *
 */

#ifndef _V_WAF_UTIL_H_INCLUDED_
#define _V_WAF_UTIL_H_INCLUDED_

typedef void vfp_init_cb_f(struct busyobj *bo);

int vwaf_util_is_waf(struct busyobj *bo);
void vwaf_util_set_vfp_cb(struct busyobj *bo, vfp_init_cb_f *vfp_init_cb);
void vwaf_util_call_vfp_cb(struct busyobj *bo);

#endif	/* _V_WAF_UTIL_H_INCLUDED_ */
