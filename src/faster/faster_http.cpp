#include <cstdarg>
#include <ctime>
#define v_noreturn_ __attribute__((__noreturn__))
extern "C" {
# include <vas.h>
# include <miniobj.h>
# include <vdef.h>
# include <vre.h>
# include <vrt.h>
# include <vtim.h>
# include <vapi/vsl_int.h>
	unsigned WS_ReserveAll(struct ws *);
	void WS_Release(struct ws *ws, unsigned bytes);
	void *WS_Alloc(struct ws *ws, unsigned bytes);
	void *WS_Copy(struct ws *ws, const void *str, int len);
	char *WS_Printf(struct ws *ws, const char *fmt, ...);
	void VSLb(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, ...);
	void http_fail(const struct http *hp);
	void http_VSLH(const struct http *hp, unsigned hdr);
}
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <fmt/compile.h>


struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	char			id[4];		/* identity */
	char			*s;		/* (S)tart of buffer */
	char			*f;		/* (F)ree/front pointer */
	char			*r;		/* (R)eserved length */
	char			*e;		/* (E)nd of buffer */
};
inline char* WS_Front(const struct ws *ws) {
	return ws->f;
}

typedef struct {
	const char		*b;
	const char		*e;
} txt;

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	uint16_t		shd;		/* Size of hd space */
	txt			*hd;
	unsigned char		*hdf;
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */

	/* NB: ->nhd and below zeroed/initialized by http_Teardown */
	uint16_t		nhd;		/* Next free hd */

	enum VSL_tag_e		logtag;		/* Must be SLT_*Method */
	struct vsl_log		*vsl;

	struct ws		*ws;
	uint16_t		status;
	uint8_t			protover;
	uint8_t			conds;		/* If-* headers present */
};


__attribute__((cold, noinline))
static void http_printf_error(struct http *to, const char *str, unsigned len1, unsigned len2)
{
	const int len = (len1 < len2) ? len1 : len2;
	http_fail(to);
	VSLb(to->vsl, SLT_LostHeader, "%.*s", len, str);
	WS_Release(to->ws, 0);
}

extern "C" __attribute__((hot))
void http_PrintfHeader(struct http *to, const char *fmt, ...)
{
	va_list ap;
	unsigned n = 0;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	const unsigned maxlen = WS_ReserveAll(to->ws);
	char* begin = WS_Front(to->ws);

	va_start(ap, fmt);
	if (fmt == (const char *)"X-Varnish: %u")
	{
		auto it = fmt::format_to_n(begin, maxlen,
			FMT_COMPILE("X-Varnish: {}"), va_arg(ap, unsigned int));
		n = it.out - begin;
	}
	else if (fmt == (const char *)"Content-Length: %zd")
	{
		auto it = fmt::format_to_n(begin, maxlen,
			FMT_COMPILE("Content-Length: {}"), va_arg(ap, ssize_t));
		n = it.out - begin;
	}
	else if (fmt == (const char *)"X-Forwarded-For: %s")
	{
		auto it = fmt::format_to_n(begin, maxlen,
			FMT_COMPILE("X-Forwarded-For: {}"), va_arg(ap, const char*));
		n = it.out - begin;
	}
	else {
		n = vsnprintf(begin, maxlen, fmt, ap);
	}
	va_end(ap);

	if (V_UNLIKELY(n + 1 >= maxlen || to->nhd >= to->shd)) {
		http_printf_error(to, begin, n, maxlen);
		return;
	}
	begin[n] = 0;
	to->hd[to->nhd].b = begin;
	to->hd[to->nhd].e = begin + n;
	to->hdf[to->nhd] = 0;
	WS_Release(to->ws, n + 1);
	http_VSLH(to, to->nhd);
	to->nhd++;
}

extern "C" __attribute__((hot))
void http_TimeHeader(struct http *to, const char *str, double now)
{
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (V_UNLIKELY(to->nhd >= to->shd)) {
		VSLb(to->vsl, SLT_LostHeader, "%s", str);
		http_fail(to);
		return;
	}
	const size_t fmtlen = __builtin_strlen(str) + VTIM_FORMAT_SIZE;
	auto* p = (char*) WS_Alloc(to->ws, fmtlen);
	if (V_UNLIKELY(p == NULL)) {
		http_fail(to);
		VSLb(to->vsl, SLT_LostHeader, "%s", str);
		return;
	}
	auto it = fmt::format_to_n(p, fmtlen,
		// Weekname, day monthname year hour:minute:second
		"{} {:%A, %d %h %Y %T} GMT", str, fmt::gmtime(now));
	*it.out = '\0';
	to->hd[to->nhd].b = p;
	to->hd[to->nhd].e = it.out;
	to->hdf[to->nhd] = 0;
	http_VSLH(to, to->nhd);
	to->nhd++;
}


extern "C" {
	void vsl_sanity(const struct vsl_log *vsl);
	void vslr(enum VSL_tag_e tag, uint32_t vxid, const char *b, unsigned len);
	void VSLbv(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, va_list ap);
	void VSLbt(struct vsl_log *vsl, enum VSL_tag_e tag, txt t);
	void VSL_Flush(struct vsl_log *vsl, int overflow);
	#include <common/common_param.h>	/* struct params */
	#include <vsl_priv.h>
	extern struct params *cache_param;
	struct vsl_log {
		uint32_t                *wlb, *wlp, *wle;
		unsigned                wlr;
		unsigned                wid;
	};
}

inline int
vsl_tag_is_masked(enum VSL_tag_e tag)
{
	volatile uint8_t *bm = &cache_param->vsl_mask[0];
	uint8_t b;

	assert(tag > SLT__Bogus);
	assert(tag < SLT__Reserved);
	bm += ((unsigned)tag >> 3);
	b = (0x80 >> ((unsigned)tag & 7));
	return (*bm & b);
}

extern "C"
void VSLv(enum VSL_tag_e tag, uint32_t vxid, const char *fmt, va_list ap)
{
	unsigned n, mlen = cache_param->vsl_reclen;
	char buf[mlen];

	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;

	if (strchr(fmt, '%') == NULL) {
		vslr(tag, vxid, fmt, strlen(fmt) + 1);
	} else {
		n = vsnprintf(buf, mlen, fmt, ap);
		if (n > mlen - 1)
			n = mlen - 1;
		buf[n++] = '\0'; /* NUL-terminated */
		vslr(tag, vxid, buf, n);
	}

}

inline uint32_t *
vsl_hdr(enum VSL_tag_e tag, uint32_t *p, unsigned len, uint32_t vxid)
{

	AZ((uintptr_t)p & 0x3);
	assert(tag > SLT__Bogus);
	assert(tag < SLT__Reserved);
	AZ(len & ~VSL_LENMASK);

	p[1] = vxid;
	p[0] = ((((unsigned)tag & 0xff) << 24) | len);
	return (VSL_END(p, len));
}

extern "C"
void VSLbv(struct vsl_log *vsl, enum VSL_tag_e tag, const char *fmt, va_list ap)
{
	char *p;
	const char *u, *f;
	unsigned n, mlen;
	txt t;
	va_list ap2;
	int first;

	vsl_sanity(vsl);
	AN(fmt);
	if (vsl_tag_is_masked(tag))
		return;

	/*
	 * If there are no printf-expansions, don't waste time expanding them
	 */
	f = NULL;
	for (u = fmt; *u != '\0'; u++)
		if (*u == '%')
			f = u;
	if (f == NULL) {
		t.b = fmt;
		t.e = u;
		VSLbt(vsl, tag, t);
		return;
	}

	if (!strcmp(fmt, "%s")) {
		p = va_arg(ap, char *);
		t.b = p;
		t.e = strchr(p, '\0');
		VSLbt(vsl, tag, t);
		return;
	}

	assert(vsl->wlp <= vsl->wle);

	/* Flush if we can't fit any bytes */
	if (vsl->wle - vsl->wlp <= VSL_OVERHEAD)
		VSL_Flush(vsl, 1);

	/* Do the vsnprintf formatting in one or two stages. If the first
	   stage shows that we overflowed, and the available space to work
	   with was less than vsl_reclen, flush and do the formatting
	   again. */
	first = 1;
	while (1) {
		assert(vsl->wle - vsl->wlp > VSL_OVERHEAD);
		mlen = VSL_BYTES((vsl->wle - vsl->wlp) - VSL_OVERHEAD);
		if (mlen > cache_param->vsl_reclen)
			mlen = cache_param->vsl_reclen;
		assert(mlen > 0);
		assert(VSL_END(vsl->wlp, mlen) <= vsl->wle);
		p = VSL_DATA(vsl->wlp);
		va_copy(ap2, ap);
		n = vsnprintf(p, mlen, fmt, ap2);
		va_end(ap2);

		if (first && n >= mlen && mlen < cache_param->vsl_reclen) {
			first = 0;
			VSL_Flush(vsl, 1);
			continue;
		}

		break;
	}

	if (n > mlen - 1)
		n = mlen - 1;	/* we truncate long fields */
	p[n++] = '\0';		/* NUL-terminated */
	vsl->wlp = vsl_hdr(tag, vsl->wlp, n, vsl->wid);
	assert(vsl->wlp <= vsl->wle);
	vsl->wlr++;
}
