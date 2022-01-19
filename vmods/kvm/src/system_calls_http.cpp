extern "C" {
	void http_SetH(struct http *to, unsigned n, const char *fm);
	void http_UnsetIdx(struct http *hp, unsigned idx);
	unsigned HTTP_FindHdr(const struct http *hp, unsigned l, const char *hdr);
	void http_PrintfHeader(struct http *to, const char *fmt, ...);
}
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

#define HDR_FIRST     6
#define HDR_INVALID   UINT32_MAX

namespace kvm {

struct guest_header_field {
	int where;
	uint32_t index;
	bool     deleted = false;
	bool     foreach = false;
};

inline void foreach(http* hp, const std::function<void(http*, txt&, size_t)>& cb)
{
	for (size_t i = 0; i < hp->field_count; i++) {
		cb(hp, hp->field_array[i], i);
	}
}

inline bool is_invalid(unsigned idx) {
	return idx == HDR_INVALID;
}
inline bool is_valid_index(const http* hp, unsigned idx) {
	return idx < hp->field_count;
}

inline http*
get_http(VRT_CTX, int where)
{
	struct http* hp = nullptr;
	switch (where) {
	case HDR_REQ:
		hp = ctx->http_req;
		break;
	case HDR_REQ_TOP:
		hp = ctx->http_req_top;
		break;
	case HDR_BEREQ:
		hp = ctx->http_bereq;
		break;
	case HDR_BERESP:
		hp = ctx->http_beresp;
		break;
	case HDR_RESP:
		hp = ctx->http_resp;
		break;
	}
	if (UNLIKELY(hp == nullptr))
		throw std::runtime_error("Selected HTTP not available at this time: " + std::to_string(where));
	return hp;
}
inline std::tuple<http*, txt&>
get_field(VRT_CTX, int where, uint32_t index)
{
	auto* hp = get_http(ctx, (gethdr_e) where);

	if (is_valid_index(hp, index))
	{
		return {hp, hp->field_array[index]};
	}
	throw std::out_of_range("Header field index not in bounds");
}

inline uint32_t field_length(const txt& field)
{
	return field.end - field.begin;
}

static long
http_header_append(MachineInstance& inst, int where, uint64_t addr, uint32_t len)
{
	auto* hp = get_http(inst.ctx(), (gethdr_e) where);

	auto* val = (char*) WS_Alloc(inst.ctx()->ws, len+1);
	if (val == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	inst.machine().copy_from_guest(val, addr, len);
	val[len] = 0;

	if (UNLIKELY(hp->field_count >= hp->fields_max)) {
		VSLb(hp->vsl, SLT_LostHeader, "%.*s", (int) len, val);
		return HDR_INVALID;
	}

	const int idx = hp->field_count++;
	http_SetH(hp, idx, val);
	return idx;
}

static void syscall_http_append(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	regs.rax = http_header_append(inst, regs.rdi, regs.rsi, regs.rdx);
	machine.set_registers(regs);
}

} // kvm
