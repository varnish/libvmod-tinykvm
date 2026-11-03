/**
 * @file kvm_deliver.c
 * @brief Response body transformation in vcl_deliver (experimental).
 *
 * A delivery processor (VDP) cannot fail a response: by the time it sees
 * the body, the status and headers have been sent. vcl_deliver runs before
 * anything is sent, so here we copy the whole object into a VM of the given
 * program, let the program produce a new response, and swap the request's
 * object for a private transient one that delivers it, the way cnt_synth()
 * builds its body. Delivery then sees a plain, complete object:
 * Content-Length, Range and HEAD all work, and no gunzip or ESI processor
 * gets pushed. On any failure nothing has been sent, and VCL can still
 * return (synth).
 *
 * The body is always copied into the VM. A body that is still being
 * fetched is first read into a buffer, so that a slow fetch does not keep
 * a VM reserved. The response can be delivered straight out of the VM's
 * memory (zero-copy) by a view object, which then keeps the VM reserved
 * until the object is freed after delivery, or until the request reserves
 * the same program again (kvm_deliver_release()). Otherwise the response
 * is copied into the object and the VM is freed right away.
 *
 * The view object mirrors private Varnish Enterprise structures, so it is
 * only used on releases where the mirrors were checked against the source
 * (deliver_layout_known()). On others, the response is always copied.
 */
#include "vmod_kvm.h"

#include "kvm_backend.h"
#include "vcl.h"
#include "vcc_if.h"
#include "VSC_vmod_kvm.h"

extern void kvm_backend_call(VRT_CTX, KVM_SLOT,
	const struct kvm_chain_item *, struct backend_post *, struct backend_result *);
extern int kvm_deliver_copy_to_vm(struct backend_post *, const void *, ssize_t);

#ifdef VARNISH_PLUS
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Not in the installed headers (cache_objhead.h, storage.h). These have
   the same signatures in all Varnish Enterprise 6.0 releases. */
struct objcore *HSH_Private(const struct worker *wrk);
void HSH_Abandon(struct objcore *oc);
void HSH_DerefBoc(struct worker *wrk, struct objcore *);
int HSH_DerefObjCore(struct worker *, struct objcore **);
extern struct stevedore *stv_transient;
enum stv_alloc_result {
	STV_AR_OK,
	STV_AR_FALLBACK,
	STV_AR_FAIL,
};

/* The largest max_size that deliver_transform() accepts */
#define DELIVER_MAX_SIZE	(1ULL << 32)

/* The running varnishd, eg. "varnish-plus-6.0.18r5 revision <hash>".
   Weak, so that the VMOD still loads if varnishd does not export it. */
extern const char *VCS_version __attribute__((weak));

/* Releases whose struct stevedore and struct obj_methods are mirrored
   below. 6.0.17 has the same VRT version as 6.0.18r1-r4, but its struct
   stevedore has one callback less, so the VRT version cannot be used. */
static const char *const deliver_known_versions[] = {
	"varnish-plus-6.0.18r1 ",
	"varnish-plus-6.0.18r2 ",
	"varnish-plus-6.0.18r3 ",
	"varnish-plus-6.0.18r4 ",
	"varnish-plus-6.0.18r5 ",
	"varnish-plus-6.0.18r6 ",
	"varnish-plus-6.0.18r7 ",
};

/* Mirrors struct obj_methods in Varnish Enterprise */
struct deliver_obj_methods {
	void		*objgetlastlru;
	void		*objtryrefattr;
	void		*objrefattr;
	void		*objderefattr;
	void		(*objfree)(struct worker *, struct objcore *);
	int		(*objiterator)(struct worker *, struct objcore *, void *,
			    objiterate_f *, int, size_t, ssize_t);
	void		*objgetspace;
	void		*objextend;
	void		*objtrimstore;
	void		*objfinish;
	void		*objbocdone;
	void		*objslim;
	void		*objgetattr;
	void		*objsetattr;
	void		*objhasattr;
	void		*objsetauxattr;
	void		*objgetauxattr;
	void		*objrelauxattr;
	void		*objtouch;
	void		*objsetstate;
	void		*objsetlen;
	uint64_t	(*objgetlen)(struct worker *, struct objcore *);
	void		*objcheckvary;
	void		*objgetvary;
	void		*objsetvary;
	void		*objsetslicerdata;
	void		*objgetslicerdata;
	void		*objevent;
};

/* Mirrors struct stevedore in Varnish Enterprise */
struct deliver_stevedore {
	unsigned	magic;
#define DELIVER_STEVEDORE_MAGIC		0x4baf43db
	const char	*name;
	void		*callbacks[24];
	const struct deliver_obj_methods *methods;
	void		*lru;
#define VRTSTVVAR(nm, vtype, ctype, dval) void *var_##nm;
#include "tbl/vrt_stv_var.h"
	void		*priv;
	void		*list[2];
	const char	*ident;
	const char	*vclname;
	void		*vgs;
};

/* A private object whose body is read straight out of a reserved VM.
   It has its own copy of the stevedore, with the methods replaced. */
struct deliver_view {
	struct deliver_stevedore	stv;
	unsigned			magic;
#define DELIVER_VIEW_MAGIC		0x6b2e9d14
	const struct deliver_stevedore	*real;
	KVM_SLOT			slot;
	TEN_PTR				tenant;
	size_t				length;
	size_t				bufcount;
	struct VMBuffer			buffers[];
};

static const struct deliver_obj_methods *deliver_real_methods;
static struct deliver_obj_methods deliver_methods;
static pthread_once_t deliver_once = PTHREAD_ONCE_INIT;

static struct deliver_view *
deliver_view_of(const struct objcore *oc)
{
	struct deliver_view *dv;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	if (oc->stobj->stevedore == NULL)
		return (NULL);
	dv = (struct deliver_view *)(uintptr_t)oc->stobj->stevedore;
	if (dv->stv.methods != &deliver_methods)
		return (NULL);
	CHECK_OBJ(dv, DELIVER_VIEW_MAGIC);
	return (dv);
}

/* Calls func on the part [start, stop) of the buffers */
static int
deliver_iterate_buffers(const struct VMBuffer *buffers, size_t bufcount,
    size_t start, size_t stop, void *priv, objiterate_f *func)
{
	size_t off = 0;

	for (size_t i = 0; i < bufcount && off < stop; i++) {
		const size_t size = buffers[i].size;
		if (off + size <= start) {
			off += size;
			continue;
		}
		const size_t b = (start > off) ? start - off : 0;
		const size_t e = (stop - off < size) ? stop - off : size;
		const int last = (off + e == stop);
		const int ret = func(priv, 1, last, buffers[i].data + b, e - b);
		if (ret != 0)
			return (ret);
		off += size;
	}
	return (0);
}

static int
deliver_iterator(struct worker *wrk, struct objcore *oc, void *priv,
    objiterate_f *func, int final, size_t start, ssize_t end)
{
	struct deliver_view *dv = deliver_view_of(oc);
	size_t stop;

	(void)wrk;
	(void)final;
	AN(dv);
	stop = dv->length;
	if (end >= 0 && (size_t)end < stop)
		stop = end;
	if (start >= stop)
		return (func(priv, 0, 1, NULL, 0));
	return (deliver_iterate_buffers(dv->buffers, dv->bufcount,
	    start, stop, priv, func));
}

static uint64_t
deliver_getlen(struct worker *wrk, struct objcore *oc)
{
	struct deliver_view *dv = deliver_view_of(oc);

	(void)wrk;
	AN(dv);
	return (dv->length);
}

static void
deliver_objfree(struct worker *wrk, struct objcore *oc)
{
	struct deliver_view *dv = deliver_view_of(oc);

	AN(dv);
	oc->stobj->stevedore = (const struct stevedore *)(uintptr_t)dv->real;
	deliver_real_methods->objfree(wrk, oc);
	/* The response is no longer needed: free the VM */
	kvm_get_free_function()(dv->slot);
	__sync_fetch_and_sub(&kvm_zero_copy_holds, 1);
	dv->magic = 0;
	free(dv);
}

static int
deliver_layout_known(void)
{
	if (&VCS_version == NULL || VCS_version == NULL)
		return (0);
	for (size_t i = 0; i < sizeof(deliver_known_versions) /
	    sizeof(deliver_known_versions[0]); i++) {
		const char *v = deliver_known_versions[i];
		if (!strncmp(VCS_version, v, strlen(v)))
			return (1);
	}
	return (0);
}

static void
deliver_init(void)
{
	const struct deliver_stevedore *stv = (const void *)stv_transient;

	/* Leaving deliver_real_methods NULL disables zero-copy */
	if (!deliver_layout_known())
		return;
	if (stv == NULL || stv->magic != DELIVER_STEVEDORE_MAGIC ||
	    stv->methods == NULL || stv->methods->objfree == NULL ||
	    stv->methods->objiterator == NULL)
		return;
	deliver_methods = *stv->methods;
	deliver_methods.objfree = deliver_objfree;
	deliver_methods.objiterator = deliver_iterator;
	deliver_methods.objgetlen = deliver_getlen;
	deliver_real_methods = stv->methods;
}

static int
deliver_copy_to_vm(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	(void)flush;
	(void)last;
	if (len != 0)
		return (kvm_deliver_copy_to_vm(priv, ptr, len));
	return (0);
}

/* Reads a body into a growing buffer of at most limit bytes */
struct deliver_buffer {
	char			*data;
	size_t			len;
	size_t			size;
	size_t			limit;
	int			too_large;
};

static int
deliver_buffer_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct deliver_buffer *db = priv;

	(void)flush;
	(void)last;
	if (len <= 0)
		return (0);
	if ((size_t)len > db->limit - db->len) {
		db->too_large = 1;
		return (-1);
	}
	if ((size_t)len > db->size - db->len) {
		size_t size = (db->size > 0) ? db->size : 65536;
		while (size - db->len < (size_t)len)
			size *= 2;
		if (size > db->limit)
			size = db->limit;
		char *data = realloc(db->data, size);
		if (data == NULL)
			return (-1);
		db->data = data;
		db->size = size;
	}
	memcpy(db->data + db->len, ptr, len);
	db->len += len;
	return (0);
}

struct deliver_copy {
	struct worker		*wrk;
	struct objcore		*oc;
	struct stv_stat		*stvstat;
};

static int
deliver_copy_out(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct deliver_copy *dc = priv;
	const uint8_t *src = ptr;
	uint8_t *dest;
	ssize_t l2;

	(void)flush;
	(void)last;
	while (len > 0) {
		l2 = len;
		if (!ObjGetSpace(dc->wrk, dc->oc, &l2, &dest, dc->stvstat))
			return (-1);
		AN(dest);
		if (l2 > len)
			l2 = len;
		memcpy(dest, src, l2);
		ObjExtend(dc->wrk, dc->oc, l2, dc->stvstat);
		src += l2;
		len -= l2;
	}
	return (0);
}

/* Build a private transient object for a response body in buffers.
   When want_view is set and the object can be a view, *view is set and
   the body is not copied: the caller turns the object into a view.
   Returns NULL with nothing left behind on failure. */
static struct objcore *
deliver_new_object(VRT_CTX, const struct VMBuffer *buffers, size_t bufcount,
    size_t length, int want_view, oaref_t *oaref, int *view)
{
	struct req *req = ctx->req;
	struct worker *wrk = req->wrk;
	struct stv_hints hints[1];
	STV_STAT_STACKALLOC(stvstat, 64);
	int ok = 1;

	struct objcore *oc = HSH_Private(wrk);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	memset(hints, 0, sizeof *hints);
	hints->size_body = want_view ? 0 : length;
	hints->vrt_privs = req->privs;
	hints->vrt_privs_id = (uintptr_t)req;
	hints->vcl = req->vcl;
	hints->is_synth_client = 1;

	*oaref = 0;
	*view = 0;
	if (STV_NewObject(wrk, oc, stv_transient, hints, stvstat) == STV_AR_OK) {
		*oaref = ObjRefAttr(wrk, oc);
		AN(*oaref);
	} else
		ok = 0;
	if (ok && want_view && deliver_real_methods != NULL &&
	    ((const struct deliver_stevedore *)(const void *)
	    oc->stobj->stevedore)->methods == deliver_real_methods)
		*view = 1;
	else if (ok) {
		struct deliver_copy dc = {
			.wrk = wrk,
			.oc = oc,
			.stvstat = stvstat,
		};
		if (length > 0 && deliver_iterate_buffers(buffers, bufcount,
		    0, length, &dc, deliver_copy_out) != 0)
			ok = 0;
		else
			ObjSetLen(wrk, oc, *oaref, length);
	}

	HSH_DerefBoc(wrk, oc);
	if (stvstat != NULL)
		STV_Stat_log(stv_transient, req->vsl, stvstat);

	if (!ok) {
		VSLb(req->vsl, SLT_Error,
			"deliver_transform: could not store the response");
		if (*oaref)
			ObjDerefAttr(oc, oaref);
		AZ(*oaref);
		(void)HSH_DerefObjCore(wrk, &oc);
		return (NULL);
	}
	return (oc);
}

static void
deliver_drop_orig(struct worker *wrk, struct req *req, int slim)
{
	ObjDerefAttr(req->objcore, &req->oc_oaref);
	if (slim && (req->objcore->flags & OC_F_TRANSIENT)) {
		HSH_Abandon(req->objcore);
		ObjSlim(wrk, req->objcore);
	}
	(void)HSH_DerefObjCore(wrk, &req->objcore);
}

static void
deliver_swap(struct req *req, struct objcore *oc, oaref_t oaref)
{
	AZ(req->oc_oaref);
	AZ(req->objcore);
	req->objcore = oc;
	req->oc_oaref = oaref;
}

/* Replace the view from an earlier transform with a copy of its body,
   which frees the VM it holds. */
static int
deliver_materialize(VRT_CTX, struct deliver_view *prev)
{
	struct req *req = ctx->req;
	oaref_t oaref;
	int view;

	struct objcore *oc = deliver_new_object(ctx, prev->buffers,
	    prev->bufcount, prev->length, 0, &oaref, &view);
	if (oc == NULL)
		return (-1);
	AZ(view);
	deliver_drop_orig(req->wrk, req, 0);
	deliver_swap(req, oc, oaref);
	return (0);
}

/* Called before a VM of the given program is reserved (through
   kvm_task_release_holds()). When the request's object is a view from
   an earlier zero-copy transform that holds a VM of the same program,
   it is replaced by a copy, freeing the VM. Otherwise the reservation
   would wait for the VM that this request holds until it times out. */
int
kvm_deliver_release(VRT_CTX, TEN_PTR tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	/* After vcl_deliver, the object is delivered and freed */
	if (ctx->req == NULL || ctx->method != VCL_MET_DELIVER ||
	    ctx->req->objcore == NULL)
		return (0);
	struct deliver_view *dv = deliver_view_of(ctx->req->objcore);
	if (dv == NULL || dv->tenant != tenant)
		return (0);
	if (deliver_materialize(ctx, dv) < 0) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: unable to free '%s' held by an earlier transform",
			kvm_tenant_name(tenant));
		return (-1);
	}
	return (0);
}

VCL_BOOL
kvm_vm_deliver_transform(VRT_CTX, VCL_PRIV task, VCL_STRING program,
	VCL_STRING url, VCL_STRING arg, VCL_BYTES max_size, VCL_BOOL zero_copy)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(task);

	if (ctx->method != VCL_MET_DELIVER) {
		VRT_fail(ctx, "KVM: deliver_transform() is only allowed in vcl_deliver");
		return (0);
	}
	struct req *req = ctx->req;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->objcore, OBJCORE_MAGIC);
	AN(req->oc_oaref);
	struct worker *wrk = req->wrk;

	struct vmod_kvm_tenant *tenant = kvm_tenant_find(task, program);
	if (tenant == NULL) {
		VRT_fail(ctx, "KVM: Program not found: %s", program);
		__sync_fetch_and_add(&vsc_vmod_kvm->program_notfound, 1);
		return (0);
	}
	if (req->objcore->flags & OC_F_SLICED) {
		VSLb(ctx->vsl, SLT_Error, "deliver_transform: sliced objects are not supported");
		return (0);
	}
	if (req->esi_level > 0) {
		VSLb(ctx->vsl, SLT_Error, "deliver_transform: not supported in ESI includes");
		return (0);
	}
	/* max_size may come from a header, eg. with std.bytes() */
	if (max_size < 0 || (uint64_t)max_size > DELIVER_MAX_SIZE) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: max_size %jd is not between 0 and %llu",
			(intmax_t)max_size, (unsigned long long)DELIVER_MAX_SIZE);
		return (0);
	}
	const size_t limit = (size_t)max_size;
	AZ(pthread_once(&deliver_once, deliver_init));
	/* The program gets the stored bytes, so they must be the body itself */
	if (http_GetHdr(req->resp, H_Content_Encoding, NULL) ||
	    ObjCheckFlag(wrk, req->objcore, req->oc_oaref, OF_GZIPED) ||
	    ObjCheckFlag(wrk, req->objcore, req->oc_oaref, OF_BROTLIED) ||
	    ObjCheckFlag(wrk, req->objcore, req->oc_oaref, OF_ESIPROC) ||
	    ObjCheckFlag(wrk, req->objcore, req->oc_oaref, OF_VESPROC) ||
	    ObjCheckFlag(wrk, req->objcore, req->oc_oaref, OF_JSONPROC)) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: encoded or processed bodies are not supported");
		return (0);
	}

	/* The program is called like a POST with the response body */
	const char *ctype = NULL;
	if (!http_GetHdr(req->resp, H_Content_Type, &ctype))
		ctype = "";
	const struct kvm_chain_item invocation = {
		.tenant = tenant,
		.special_function = NULL,
		.inputs = {
			.method = "POST",
			.url = url ? url : "",
			.argument = arg ? arg : "",
			.content_type = ctype,
		},
		.break_status = 1000,
		.soft_reset = 0,
	};

	struct backend_result *result =
		(struct backend_result *)WS_Alloc(ctx->ws, VMBE_RESULT_SIZE);
	if (result == NULL) {
		VSLb(ctx->vsl, SLT_Error, "deliver_transform: out of workspace");
		return (0);
	}
	result->bufcount = VMBE_NUM_BUFFERS;
	result->content_address = 0;

	/* A body that is still being fetched is read before reserving a VM,
	   waiting for the fetch to finish (and failing if it fails), so that
	   a slow fetch does not keep a VM reserved. Otherwise the length is
	   known, and the body is copied straight into the VM. */
	struct deliver_buffer body = {
		.limit = limit,
	};
	const int buffered = (req->objcore->boc != NULL);
	size_t body_len;
	if (buffered) {
		if (ObjIterate(wrk, req->objcore, &body, deliver_buffer_body, 0, 0, -1) < 0) {
			if (body.too_large)
				VSLb(ctx->vsl, SLT_Error,
					"deliver_transform: body is over the %zu byte limit", limit);
			else
				VSLb(ctx->vsl, SLT_Error, "deliver_transform: could not read the body");
			free(body.data);
			return (0);
		}
		body_len = body.len;
	} else {
		const uint64_t len = ObjGetLen(wrk, req->objcore);
		if (len > limit) {
			VSLb(ctx->vsl, SLT_Error,
				"deliver_transform: body of %ju bytes is over the %zu byte limit",
				(uintmax_t)len, limit);
			return (0);
		}
		body_len = len;
	}

	/* This replaces the request's object if it is a view of the same
	   program (kvm_deliver_release()) */
	KVM_SLOT slot = kvm_temporarily_reserve_machine(ctx, tenant, 0, 0);
	if (slot == NULL) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: unable to reserve '%s'", kvm_tenant_name(tenant));
		free(body.data);
		return (0);
	}

	/* The VM gets exactly the body. Nothing is allocated for an empty
	   one, and the program gets a NULL buffer. */
	struct backend_post post = {
		.ctx = ctx,
		.slot = slot,
		.address = 0,
		.capacity = body_len,
		.length = 0,
		.inputs = invocation.inputs,
	};
	if (buffered) {
		const int ret = (body_len > 0) ?
			kvm_deliver_copy_to_vm(&post, body.data, body_len) : 0;
		free(body.data);
		body.data = NULL;
		if (ret < 0)
			goto fail;
	} else if (body_len > 0 && ObjIterate(wrk, req->objcore, &post,
	    deliver_copy_to_vm, 0, 0, -1) < 0) {
		VSLb(ctx->vsl, SLT_Error, "deliver_transform: could not read the body");
		goto fail;
	}

	kvm_backend_call(ctx, slot, &invocation, &post, result);
	/* A VM exception without an error page from the program */
	if (result->type == NULL)
		goto fail;
	if (result->bufcount == 0 && result->content_length > 0) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: streamed responses are not supported");
		goto fail;
	}
	if (result->content_length > limit) {
		VSLb(ctx->vsl, SLT_Error,
			"deliver_transform: response of %zu bytes is over the %zu byte limit",
			result->content_length, limit);
		goto fail;
	}

	oaref_t oaref;
	int view;
	struct objcore *oc = deliver_new_object(ctx, result->buffers,
	    result->bufcount, result->content_length, zero_copy, &oaref, &view);
	if (oc == NULL)
		goto fail;

	if (view) {
		const size_t bsize = result->bufcount * sizeof(struct VMBuffer);
		struct deliver_view *dv = calloc(1, sizeof(*dv) + bsize);
		if (dv == NULL) {
			VSLb(ctx->vsl, SLT_Error, "deliver_transform: out of memory");
			ObjDerefAttr(oc, &oaref);
			(void)HSH_DerefObjCore(wrk, &oc);
			goto fail;
		}
		dv->magic = DELIVER_VIEW_MAGIC;
		dv->real = (const void *)oc->stobj->stevedore;
		dv->stv = *dv->real;
		dv->stv.methods = &deliver_methods;
		/* The view frees the VM when the object is freed */
		dv->slot = slot;
		dv->tenant = tenant;
		dv->length = result->content_length;
		dv->bufcount = result->bufcount;
		memcpy(dv->buffers, result->buffers, bsize);
		oc->stobj->stevedore = (const void *)&dv->stv;
		__sync_fetch_and_add(&kvm_zero_copy_holds, 1);
		VSLb(ctx->vsl, SLT_Debug, "deliver_transform: zero-copy (%zu bytes)",
			dv->length);
	} else {
		/* The response has been copied out of the VM */
		kvm_free_reserved_machine(ctx, slot);
	}
	slot = NULL;

	/* The body was copied into the VM, so the original can go */
	deliver_drop_orig(wrk, req, 1);
	deliver_swap(req, oc, oaref);

	http_PutResponse(req->resp, "HTTP/1.1", (uint16_t)result->status, NULL);
	/* Delivery recomputes it from the object, but not for HEAD */
	http_Unset(req->resp, H_Content_Length);
	http_PrintfHeader(req->resp, "Content-Length: %zu", result->content_length);
	/* The validators were for the original body, and a 304 must not
	   let a client keep an earlier (per-client) response. Without a
	   Last-Modified, cnt_deliver() would compare If-Modified-Since with
	   the new object's, so the conditional headers are ignored. */
	http_Unset(req->resp, H_ETag);
	http_Unset(req->resp, H_Last_Modified);
	req->http->conds = 0;
	if (result->tsize > 0) {
		http_Unset(req->resp, H_Content_Type);
		http_PrintfHeader(req->resp, "Content-Type: %.*s",
			(int)result->tsize, result->type);
	}
	return (1);

fail:
	if (slot != NULL)
		kvm_free_reserved_machine(ctx, slot);
	return (0);
}
#else
VCL_BOOL
kvm_vm_deliver_transform(VRT_CTX, VCL_PRIV task, VCL_STRING program,
	VCL_STRING url, VCL_STRING arg, VCL_BYTES max_size, VCL_BOOL zero_copy)
{
	(void)task;
	(void)program;
	(void)url;
	(void)arg;
	(void)max_size;
	(void)zero_copy;
	VRT_fail(ctx, "KVM: deliver_transform() is only supported on Varnish Enterprise");
	return (0);
}
#endif
