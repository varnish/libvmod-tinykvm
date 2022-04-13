#pragma once

struct txt {
	const char* begin;
	const char* end;
};
struct http {
	unsigned       magic;
	uint16_t       fields_max;
	txt*           field_array;
	unsigned char* field_flags;
	uint16_t       field_count;
	int some_shit;
	struct vsl_log*vsl;
	void*          ws;
	uint16_t       status;
	uint8_t        protover;
	uint8_t        conds;
};

void http_SetStatus(struct http *hp, uint16_t status);
void http_SetH(struct http *to, unsigned n, const char *fm);
void http_UnsetIdx(struct http *hp, unsigned idx);
unsigned HTTP_FindHdr(const struct http *hp, unsigned l, const char *hdr);
void http_PrintfHeader(struct http *to, const char *fmt, ...);
void riscv_SetCacheable(VRT_CTX, bool a);
bool riscv_GetCacheable(VRT_CTX);
void riscv_SetTTL(VRT_CTX, float ttl);
float riscv_GetTTL(VRT_CTX);
long riscv_SetBackend(VRT_CTX, VCL_BACKEND);

#define HDR_FIRST     6
#define HDR_INVALID   UINT32_MAX
