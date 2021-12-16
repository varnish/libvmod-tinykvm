/*
 * Copyright (c) 2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Guillaume Quintard <guillaume@varnish-software.com>
 */

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <arpa/inet.h>

#include <modsecurity/intervention.h>
#include <modsecurity/transaction.h>
#include <modsecurity/rules_set.h>

#include <cache/cache_varnishd.h>
#include <cache/cache_filter.h>
#include "foreign/vnum.h"
#include <vcl.h>
#include <vsa.h>

#include <vrt_obj.h>
#include "foreign/vmod_util/vmod_util.h"
#include "foreign/vmod_util/waf_util.h"

#include "vcc_waf_if.h"

#define WAF_VERSION			"6.0.0"
#define COMBINED_VERSION		WAF_VERSION"_"MODSECURITY_VERSION"."\
					MODSECURITY_TAG_NUM

static const void *id;
typedef void vfp_init_waf_cb_f(struct busyobj *bo);

struct vmod_waf_init {
	unsigned			magic;
#define WAF_MAGIC			0x98fef994
	RulesSet			*rules;
	ModSecurity			*modsec;
};

struct task_ctx {
	unsigned			magic;
#define TSK_MAGIC			0x04154d5a
	Transaction			*txn;
	ModSecurityIntervention		ivn;
	vfp_init_waf_cb_f		*vfp_init_waf_cb;
	int				txn_processed;
	int				body_checked;
	ssize_t				req_bodybytes;
};

#define NOT_IN_VCL_MOD_R(SUBMOD, SUBMOD_STRING, RET)			\
	if (ctx->method != SUBMOD) {					\
		VRT_fail(ctx, "WAF: cannot call %s outside of %s",	\
			__FUNCTION__, SUBMOD_STRING);			\
		return (RET);						\
	}

#define NOT_IN_VCL_MOD(SUBMOD, SUBMOD_STRING)				\
	if (ctx->method != SUBMOD) {					\
		VRT_fail(ctx, "WAF: cannot call %s outside of %s",	\
			__FUNCTION__, SUBMOD_STRING);			\
		return;							\
	}

/* MSI will leak if msc_intervention() is called repeatedly unless log and url
 * are freed each time. The object returned alloc's the log and url on each
 *  call. msc_intervention() should only be hit once per req
 */
static int
intercepted (struct task_ctx *tctx)
{
	CHECK_OBJ_NOTNULL(tctx, TSK_MAGIC);

	if (tctx->ivn.disruptive)
		return (tctx->ivn.disruptive);

	return (msc_intervention(tctx->txn, &tctx->ivn));
}

static enum vfp_status v_matchproto_(vfp_init_f)
init_waf(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct task_ctx *tctx;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(tctx, vfe->priv1, TSK_MAGIC);

	if (intercepted(tctx))
		return (VFP_Error(vc, "WAF: Intercepted the transaction"));
	else
		return (VFP_OK);
}

static enum vfp_status v_matchproto_(vfp_pull_f)
pull_waf(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p, ssize_t *lp)
{
	struct task_ctx *tctx;
	enum vfp_status vp;
	int ret;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(tctx, vfe->priv1, TSK_MAGIC);
	AN(p);
	AN(lp);

	vp = VFP_Suck(vc, p, lp);
	if (vp == VFP_ERROR)
		return (vp);

	if (!tctx->body_checked) {
		ret = msc_append_response_body(tctx->txn, p, *lp);
		if (intercepted(tctx)) {
			VSLb(vc->bo->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
			return (VFP_Error(vc, "WAF: Intercepted the body"));
		}
		if (!ret || vp == VFP_END) {
			if (!msc_process_response_body(tctx->txn)) {
				return (VFP_Error(vc, "WAF: Failed to process"
					" body"));
			}
			tctx->body_checked = 1;
			if (intercepted(tctx)) {
				VSLb(vc->bo->vsl, SLT_WAF, "LOG: %s",
					tctx->ivn.log);
				return (VFP_Error(vc, "WAF: Intercepted on "
					"MS phase 4"));
			}
		}
	}
	return (vp);
}

static void v_matchproto_(vfp_fini_f)
fini_waf(struct vfp_ctx *vc, struct vfp_entry *vfe)
{
	struct task_ctx *tctx;
	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vfe, VFP_ENTRY_MAGIC);
	CAST_OBJ_NOTNULL(tctx, vfe->priv1, TSK_MAGIC);

	VSLb(vc->bo->vsl, SLT_WAF, "RespBody: %lu",
		vc->bo->acct.beresp_bodybytes);
}

const struct vfp vfp_waf = {
	.name = "WAF",
	.init = init_waf,
	.pull = pull_waf,
	.fini = fini_waf,
};

static void
init_vfp(struct busyobj *bo)
{
	struct vfp_entry *vfe;
	struct vmod_priv *priv;
	struct task_ctx *tcx;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->vfc, VFP_CTX_MAGIC);

	priv = vmod_util_get_priv_task(NULL, bo, &id);
	AN(priv);

	CAST_OBJ_NOTNULL(tcx, priv->priv, TSK_MAGIC);
	vfe = VFP_Push(bo->vfc, &vfp_waf);

	if (!vfe) {
		return;
	}

	CHECK_OBJ(vfe, VFP_ENTRY_MAGIC);
	vfe->priv1 = tcx;
}

static void
free_tctx(void *priv)
{
	struct task_ctx *tctx;

	CAST_OBJ_NOTNULL(tctx, priv, TSK_MAGIC);

	free(tctx->ivn.url);
	free(tctx->ivn.log);
	msc_transaction_cleanup(tctx->txn);
	FREE_OBJ(tctx);
}

typedef int (*add_hdr)(Transaction *, const u_char *, size_t, const u_char *,
	size_t);

static int
loadhdrs(VRT_CTX, const struct http *hp, Transaction *t, add_hdr func)
{
	unsigned u;
	int count = 0;
	const char *p, *q;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;

		count++;

		/* copy the header name */
		p = strchr(hp->hd[u].b, ':');
		AN(p);

		if (p == hp->hd[u].e)
			q = p;
		else
			q = p + 1;

		while(q < hp->hd[u].e && isblank(*q))
			q++;

		if (!func(t, (const u_char *)hp->hd[u].b, p - hp->hd[u].b,
			(const u_char *)q, hp->hd[u].e - q)) {
			VRT_fail(ctx, "WAF: Can't load headers");
			return(-1);
		}
	}

	return(count);
}

#define CHK_CALLSITE(ctx, hp, type)					\
	do {								\
		if (ctx->http_##type)					\
			hp = ctx->http_##type;				\
		else if (ctx->http_be##type)				\
			hp = ctx->http_be##type;			\
		else {							\
			VRT_fail(ctx, "WAF: Invalid call site");	\
			return (0);					\
		}							\
		CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);			\
	} while(0);

static int
aggregate_body(void *priv, int flush, int last, const void *ptr, ssize_t len)
{
	struct task_ctx *ctx;

	CAST_OBJ_NOTNULL(ctx, priv, TSK_MAGIC);
	AN(ctx->txn);

	(void) flush;
	(void) last;

	ctx->req_bodybytes += len;
	return (!msc_append_request_body(ctx->txn, ptr, len));
}

static void
get_ip_addr(VCL_IP ip, char *buf, long int *port)
{
	const struct sockaddr *sa;
	const struct sockaddr_in *sa4;
	const struct sockaddr_in6 *sa6;
	socklen_t len;

	AN(buf);

	if (!ip) {
		buf[0] = '\0';
		if (port)
			port = 0;
	}

	sa = VSA_Get_Sockaddr(ip, &len);

	if (sa->sa_family == AF_INET) {
		sa4 = (const void *)sa;
		inet_ntop(AF_INET, &sa4->sin_addr, buf, INET6_ADDRSTRLEN);
		if (port)
			*port = ntohs(sa4->sin_port);
	} else {
		sa6 = (const void *)sa;
		inet_ntop(AF_INET6, &sa6->sin6_addr, buf, INET6_ADDRSTRLEN);
		if (port)
			*port = ntohs(sa6->sin6_port);
	}
}

VCL_STRING
vmod_version(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (COMBINED_VERSION);
}


/*
 * TODO: check if this callback message gets better to use instead of logging
 * after an interception by hand
 * I am leaving this commented because right now, it isn't printed better than
 * what we do already after intercepting but also removes a few extra lines
 * from the VSL
 */
static void
msc_callback(void *data, const void *message)
{
	(void) data;
	(void) message;
	//struct vsl_log *vsl;
	//vsl = struct vsl_log *(data);
	//VSLb(vsl, SLT_WAF, "%s", (const char *)message);
}

VCL_VOID
vmod_init__init(VRT_CTX, struct vmod_waf_init **objp, const char *vcl_name)
{
	struct vmod_waf_init *waf_conf;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	NOT_IN_VCL_MOD(VCL_MET_INIT, "vcl_init");

	ALLOC_OBJ(waf_conf, WAF_MAGIC);
	AN(waf_conf);

	(void) vcl_name;

	waf_conf->modsec = msc_init();
	waf_conf->rules = msc_create_rules_set();

	msc_set_log_cb(waf_conf->modsec, msc_callback);

	msc_set_connector_info(waf_conf->modsec, "Varnish WAF "
		COMBINED_VERSION);

	*objp = waf_conf;
}

VCL_VOID
vmod_init__fini(struct vmod_waf_init **objp)
{
	struct vmod_waf_init *waf_conf;

	AN(objp);
	CAST_OBJ_NOTNULL(waf_conf, *objp, WAF_MAGIC);

	msc_rules_cleanup(waf_conf->rules);
	msc_cleanup(waf_conf->modsec);
	FREE_OBJ(*objp);
	*objp = NULL;
}

VCL_VOID
vmod_init_add_files(VRT_CTX, struct vmod_waf_init *waf, VCL_STRING path)
{
	const char *err = NULL;
	glob_t pglob;
	size_t i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);
	NOT_IN_VCL_MOD(VCL_MET_INIT, "vcl_init");

	if (!path)
		return;

	if (glob(path, GLOB_ERR, NULL, &pglob)) {
		VRT_fail(ctx, "WAF: Error setting up glob: %s %d %s", path,
			errno, strerror(errno));
		return;
	}

	for (i = 0; i < pglob.gl_pathc; i++) {
		VSL(SLT_WAF, 0, "FILE: %s", pglob.gl_pathv[i]);
		if (msc_rules_add_file(waf->rules, pglob.gl_pathv[i], &err)
			<= 0) {
			VRT_fail(ctx, "WAF: %s", err);
			break;
		}
	}

	globfree(&pglob);
}

/* This function works by configuring the backend to only allow access if the
 * requests contains a header `ModSec-key` with the specified key. If the
 * backend does not force a header, no key is required.
 */

VCL_VOID
vmod_init_add_file_remote(VRT_CTX, struct vmod_waf_init *waf,
	VCL_STRING url, VCL_STRING key)
{
	const char *err = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);
	NOT_IN_VCL_MOD(VCL_MET_INIT, "vcl_init");

	if (!url || !key)
		return;

	VSL(SLT_WAF, 0, "URL: %s", url);
	if (msc_rules_add_remote(waf->rules, key, url, &err) <= 0) {
		VRT_fail(ctx, "WAF: %s", err);
	}
}

/* Set up the transaction for the single HTTP Request.
 * This is needed to allow skipping one of request or response.
 * This should be called before check_req/sp
 */

VCL_VOID
vmod_init_init_transaction(VRT_CTX, struct vmod_waf_init *waf)
{
	struct task_ctx *tctx;
	struct vmod_priv *tpriv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);

	tpriv = VRT_priv_task(ctx, waf);
	AN(tpriv);

	/* Free previous instance. XXX: This is not OK. */
	if (tpriv->priv) {
		AN(tpriv->free);
		tpriv->free(tpriv->priv);
		tpriv->priv = NULL;
		tpriv->free = NULL;
	}

	ALLOC_OBJ(tctx, TSK_MAGIC);
	AN(tctx);
	tpriv->priv = tctx;
	tpriv->free = free_tctx;

	AN(waf->modsec);
	AN(waf->rules);

	/*
	 * TODO: do something with the last arg to log?
	 * note: this pointer is to pass data to the callback function
	 * This would be ctx to get vsl in this case to be able to print pretty
	 */
	tctx->txn = msc_new_transaction(waf->modsec, waf->rules, NULL);

	/* Poor mans false. We need to make sure that the transaction has been
	   initialized properly before we can do response checking. */
	tctx->txn_processed = 0;
}

VCL_BOOL
vmod_init_check_req(VRT_CTX, struct vmod_waf_init *waf,
	VCL_STRING client_ip, VCL_INT client_port)
{
	long int server_port = 0;
	char server_ip[INET6_ADDRSTRLEN];
	const char *proto_num;
	int header_count = 0;
	const struct http *hp;
	struct task_ctx *tctx;
	struct vmod_priv *tpriv;
	VCL_IP ip;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);

	CHK_CALLSITE(ctx, hp, req);

	tpriv = VRT_priv_task(ctx, waf);
	CAST_OBJ(tctx, tpriv->priv, TSK_MAGIC);

	/* Check if being called in right order */
	if (!tctx) {
		VRT_fail(ctx, "WAF: .req_intercept() wasn't called after "
			".init_transaction()");
		return (0);
	}

	if (!client_ip)
		client_ip = "";

	/* Get Server IP */
	ip = VRT_r_local_ip(ctx);
	get_ip_addr(ip, server_ip, &server_port);

	if (!msc_process_connection(tctx->txn, client_ip, client_port,
		server_ip, server_port)) {
		VSLb(ctx->vsl, SLT_WAF, "ERROR: processing server connection");
		return (0);
	}
	/* The transaction has been processed. */
	tctx->txn_processed = 1;

	if (intercepted(tctx)) {
		VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
		return (1);
	}

	proto_num = strchr(hp->hd[HTTP_HDR_PROTO].b, '/');

	if (!proto_num)
		proto_num = "";
	else
		proto_num++;

	if (!msc_process_uri(tctx->txn, hp->hd[HTTP_HDR_URL].b,
		hp->hd[HTTP_HDR_METHOD].b, proto_num)) {
		VSLb(ctx->vsl, SLT_WAF, "ERROR: Failed to process URI");
		return (0);
	}
	VSLb(ctx->vsl, SLT_WAF, "proto: %s %s %s", hp->hd[HTTP_HDR_URL].b,
		hp->hd[HTTP_HDR_METHOD].b, hp->hd[HTTP_HDR_PROTO].b);

	if (intercepted(tctx)) {
		VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
		return (1);
	}

	header_count = loadhdrs(ctx, hp, tctx->txn, msc_add_n_request_header);

	if (header_count == -1) {
		VRT_fail(ctx, "WAF: Couldn't load ReqHeaders");
		return (0);
	}

	VSLb(ctx->vsl, SLT_WAF, "ReqHeaders: %d", header_count);

	if (!msc_process_request_headers(tctx->txn)) {
		VSLb(ctx->vsl, SLT_WAF, "ERROR: Failed to process ReqHeaders");
		return (0);
	}

	if (intercepted(tctx)) {
		VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
		return (1);
	}

	if (ctx->req) {
		CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);

		if (VRB_Iterate(ctx->req, aggregate_body, tctx) < 0) {
			VSLb(ctx->vsl, SLT_WAF, "ERROR: Iteration on req.body "
				"didn't succeed");
			return (0);
		}
	} else if (ctx->bo) {
		CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

		if (ctx->bo->initial_req_body_status == REQ_BODY_CACHED &&
			ctx->bo->bereq_body) {
			CHECK_OBJ_NOTNULL(ctx->bo->bereq_body, OBJCORE_MAGIC);
			CHECK_OBJ_NOTNULL(ctx->bo->wrk, WORKER_MAGIC);

			if (ObjIterate(ctx->bo->wrk, ctx->bo->bereq_body,
				tctx, aggregate_body, 0, 0, -1) < 0) {
				VSLb(ctx->vsl, SLT_WAF, "ERROR: "
					"Iteration on req.body didn't succeed");
				return (0);
			}
		}
	} else {
		WRONG("Invalid call location");
	}

	VSLb(ctx->vsl, SLT_WAF, "ReqBody: %ld", tctx->req_bodybytes);

	if (!msc_process_request_body(tctx->txn)) {
		VSLb(ctx->vsl, SLT_WAF, "ERROR: Failed to process ReqBody");
		return (0);
	}

	if (intercepted(tctx)) {
		VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
		return (1);
	}

	return (0);
}

VCL_BOOL
vmod_init_check_resp(VRT_CTX, struct vmod_waf_init *waf)
{
	int header_count = 0;
	const struct http *hp;
	struct task_ctx *tctx;
	struct vmod_priv *priv, *tpriv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);
	NOT_IN_VCL_MOD_R(VCL_MET_BACKEND_RESPONSE, "vcl_backend_response", 0);
	CHECK_OBJ_NOTNULL(ctx->bo, BUSYOBJ_MAGIC);

	CHK_CALLSITE(ctx, hp, resp);

	tpriv = VRT_priv_task(ctx, waf);
	CAST_OBJ(tctx, tpriv->priv, TSK_MAGIC);

	/* Check if being called in right order */
	if (!tctx) {
		VRT_fail(ctx, "WAF: .resp_intercept() wasn't called after "
			".req_intercept()");
		return (0);
	}

	if (!tctx->txn_processed) {
		VRT_fail(ctx, "WAF: Transaction not initialized. "
			"Did you forget .check_req()?");
		return (0);
	}

	if (intercepted(tctx)) {
		 VSLb(ctx->vsl, SLT_WAF, "ERROR: Transaction already "
			"intercepted");
		return (1);
	}

	header_count = loadhdrs(ctx, hp, tctx->txn, msc_add_n_response_header);

	if (header_count == -1) {
		VRT_fail(ctx, "WAF: Couldn't load RespHeaders");
		return (0);
	}

	VSLb(ctx->vsl, SLT_WAF, "RespHeaders: %d", header_count);

	if (!msc_process_response_headers(tctx->txn, hp->status,
		hp->hd[HTTP_HDR_PROTO].b)) {
		VSLb(ctx->vsl, SLT_WAF, "ERROR: Failed to process RespHeaders");
		return (0);
	}

	if (intercepted(tctx)) {
		VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
		return (1);
	}

	/* If no body to be read. Make sure to call phase 4 */
	if (ctx->bo->htc->body_status == BS_NONE) {
		VSLb(ctx->vsl, SLT_WAF, "RespBody: 0");

		if (!msc_process_response_body(tctx->txn)) {
			VSLb(ctx->vsl, SLT_WAF, "ERROR: Failed to process "
				"RespBody");
			return (0);
		}

		if (intercepted(tctx)) {
			VSLb(ctx->vsl, SLT_WAF, "LOG: %s", tctx->ivn.log);
			return (1);
		}
	} else {
		/* Set the vfp */
		vwaf_util_set_vfp_cb(ctx->bo, init_vfp);

		/* Register the data too */
		priv = vmod_util_get_priv_task(NULL, ctx->bo, &id);
		AN(priv);
		priv->priv = tctx;
	}
	return (0);
}

VCL_VOID
vmod_init_audit_log(VRT_CTX, struct vmod_waf_init *waf)
{
	struct task_ctx *tctx;
	struct vmod_priv *tpriv;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);

	tpriv = VRT_priv_task(ctx, waf);
	CAST_OBJ(tctx, tpriv->priv, TSK_MAGIC);

	/* Check if being called in right order */
	if (!tctx) {
		VRT_fail(ctx, "WAF: .audit_log() wasn't called after "
			".req_intercept()");
	} else if (!msc_process_logging(tctx->txn))
		VSLb(ctx->vsl, SLT_WAF, "ERROR: failed to process audit log");
}

#define get_field(n, v, t)						\
VCL_ ##t								\
vmod_init_ ##n(VRT_CTX, struct vmod_waf_init *waf)			\
{									\
	struct task_ctx *tctx;						\
	struct vmod_priv *tpriv;					\
									\
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);				\
	CHECK_OBJ_NOTNULL(waf, WAF_MAGIC);				\
	tpriv = VRT_priv_task(ctx, waf);				\
	AN(tpriv);							\
	CAST_OBJ(tctx, tpriv->priv, TSK_MAGIC);				\
									\
	if (tctx)							\
		return (tctx->ivn.v);					\
	else								\
		return (0);						\
}

get_field(disruptive, disruptive, BOOL)
get_field(disrupt_status, status, INT)
get_field(disrupt_url, url, STRING)
get_field(disrupt_log, log, STRING)

VCL_BYTES
vmod_bytes(VRT_CTX, VCL_STRING b, VCL_BYTES d)
{
	uintmax_t ret;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (VNUM_2bytes(b, &ret, 0)) {
		return (d);
	}

	return (ret);
}
