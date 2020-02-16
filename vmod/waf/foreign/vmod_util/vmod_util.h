/*-
 * Copyright (c) 2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Reza Naghibi <reza@varnish-software.com>
 *
 */

#ifndef _V_VMOD_UTIL_H_INCLUDED_
#define _V_VMOD_UTIL_H_INCLUDED_

struct vmod_priv *vmod_util_get_priv_task(struct req *req, struct busyobj *bo,
    const void *id);

#endif	/* _V_VMOD_UTIL_H_INCLUDED_ */
