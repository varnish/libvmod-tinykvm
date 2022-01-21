#include "vmod_kvm.h"

#include "kvm_backend.h"
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include "vcl.h"
#include "vcc_if.h"

extern int kvm_backend_stream(struct backend_post *, const void*, ssize_t, int);

static int
kvm_get_aggregate_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct backend_post *post = (struct backend_post *)priv;
	(void)flush;

	if (post->process_func == 0x0) {
		/* Collect all data into one buffer inside guest VM */
		int res = kvm_copy_to_machine(
			post->slot, post->address + post->length, ptr, len);
		post->length += len;
		return (res);
	}
	/* Streaming POST */
	int res = kvm_backend_stream(post, ptr, len, last);
	post->length += len;
	return (res);
}

void kvm_get_body(struct backend_post *post, struct busyobj *bo)
{
	post->length = 0;
	if (bo->req)
		VRB_Iterate(bo->req, kvm_get_aggregate_body, post);
	else if (bo->bereq_body)
		ObjIterate(bo->wrk, bo->bereq_body, post,
			kvm_get_aggregate_body, 0, 0, -1);
}
