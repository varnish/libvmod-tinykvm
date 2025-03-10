/**
 * @file kvm_post.c
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Callbacks for Varnish POST body aggregation.
 * @version 0.1
 * @date 2022-10-10
 * 
 * Routines for retrieving request bodies either all at once, or
 * in parts (streaming POST).
 * 
 */
#include "vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vcl.h"
#include "vcc_if.h"

extern int kvm_backend_streaming_post(struct backend_post *, const void*, ssize_t);

#ifdef VARNISH_PLUS
static int
kvm_get_aggregate_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct backend_post *post = (struct backend_post *)priv;
	(void)flush;
	(void)last;

	/* We will want to call backend stream once per segment, and not
	   finally with len=0 and last=1. Instead we can use the on_post
	   callback to trigger any finishing logic. The on_post callback
	   will get called right after returning from here. */
	if (len != 0)
		return (kvm_backend_streaming_post(post, ptr, len));
	else
		return (0);
}
#else // open-source
static int
kvm_get_aggregate_body(void *priv, unsigned flush, const void *ptr, ssize_t len)
{
	struct backend_post *post = (struct backend_post *)priv;
	(void)flush;

	/* We will want to call backend stream once per segment, and not
	   finally with len=0 and last=1. Instead we can use the on_post
	   callback to trigger any finishing logic. The on_post callback
	   will get called right after returning from here. */
	if (len != 0)
		return (kvm_backend_streaming_post(post, ptr, len));
	else
		return (0);
}
#endif

int kvm_get_body(struct backend_post *post, struct busyobj *bo)
{
	post->length = 0;
#ifdef VARNISH_PLUS
	if (bo->req)
		return (VRB_Iterate(bo->req, kvm_get_aggregate_body, post));
	else if (bo->bereq_body)
		return (ObjIterate(bo->wrk, bo->bereq_body, post,
			kvm_get_aggregate_body, 0, 0, -1));
#else
	if (bo->req)
		return (VRB_Iterate(bo->wrk, bo->vsl, bo->req, kvm_get_aggregate_body, post));
	else if (bo->bereq_body)
		return (ObjIterate(bo->wrk, bo->bereq_body, post,
			kvm_get_aggregate_body, 0));
#endif
	return (-1);
}
