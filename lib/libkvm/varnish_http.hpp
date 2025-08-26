#include <cstdint>
#include <span>
extern "C" {
	void http_SetH(struct http *to, unsigned n, const char *fm);
	void http_UnsetIdx(struct http *hp, unsigned idx);
	void http_PrintfHeader(struct http *to, const char *fmt, ...);
	struct ws;
	struct ws *kvm_CreateWorkspace(const char *id, unsigned size);
	void kvm_FreeWorkspace(struct ws *ws);
enum {
#define SLTH(tag, ind, req, resp, sdesc, ldesc)	ind,
#include "tbl/vsl_tags_http.h"
};
struct easy_txt {
	const char* begin;
	const char* end;
};
void VSLbt(struct vsl_log *vsl, enum VSL_tag_e tag, easy_txt t);
}
struct http {
	unsigned       magic;
	uint16_t       fields_max;
	easy_txt*      field_array;
	unsigned char* field_flags;
	uint16_t       field_count;
	int            logtag;
	struct vsl_log*vsl;
	void*          ws;
	uint16_t       status;
	uint8_t        protover;
	uint8_t        conds;
};

#define HDR_FIRST     5
#define HDR_INVALID   UINT32_MAX
