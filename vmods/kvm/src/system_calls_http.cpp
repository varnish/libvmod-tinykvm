extern "C" {
	void http_SetH(struct http *to, unsigned n, const char *fm);
	void http_UnsetIdx(struct http *hp, unsigned idx);
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

#define HDR_FIRST     5
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
	if (ctx == nullptr) {
		throw std::runtime_error("Missing VRT_CTX in get_http");
	}
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
static unsigned
http_findhdr(const struct http *hp, unsigned l, const char *hdr)
{
	for (unsigned u = HDR_FIRST; u < hp->field_count; u++)
	{
		if (hp->field_array[u].end < hp->field_array[u].begin + l + 1)
			continue;
		if (hp->field_array[u].begin[l] != ':')
			continue;
		if (strncasecmp(hdr, hp->field_array[u].begin, l))
			continue;
		return u;
	}
	return 0;
}
static void
http_unsetat(struct http *hp, unsigned idx)
{
	unsigned count;

	assert(idx >= HDR_FIRST); /* Avoid proto fields */
	assert(idx < hp->field_count); /* Out of bounds */

	/* Inefficient, but preserves order (assuming sorted?) */
	count = hp->field_count - idx - 1;
	memmove(&hp->field_array[idx], &hp->field_array[idx + 1], count * sizeof *hp->field_array);
	memmove(&hp->field_flags[idx], &hp->field_flags[idx + 1], count * sizeof *hp->field_flags);
	hp->field_count--;
}

inline uint32_t field_length(const txt& field)
{
	return field.end - field.begin;
}

static unsigned
http_header_append(struct http* hp, const char* val, uint32_t len)
{
	if (UNLIKELY(hp->field_count >= hp->fields_max)) {
		VSLb(hp->vsl, SLT_LostHeader, "%.*s", (int) len, val);
		return HDR_INVALID;
	}

	const unsigned idx = hp->field_count++;
	http_SetH(hp, idx, val);
	return idx;
}

static void syscall_http_append(Machine& machine, MachineInstance& inst)
{
	auto regs = machine.registers();
	auto *hp = get_http(inst.ctx(), (gethdr_e)regs.rdi);
	const uint64_t addr = regs.rsi;
	const uint32_t len = regs.rdx;

	auto *val = (char *)WS_Alloc(inst.ctx()->ws, len + 1);
	if (val == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	inst.machine().copy_from_guest(val, addr, len);
	val[len] = 0;

	regs.rax = http_header_append(hp, val, len);
	machine.set_registers(regs);
}

static void syscall_http_set(Machine &machine, MachineInstance &inst)
{
	auto regs = machine.registers();
	const int where = regs.rdi;
	const uint64_t g_what = regs.rsi;
	const uint16_t g_wlen = regs.rdx;
	if (UNLIKELY(g_what == 0x0 || g_wlen == 0)) {
		regs.rax = 0;
		machine.set_registers(regs);
		return;
	}

	auto *hp = get_http(inst.ctx(), (gethdr_e)where);

	/* Read out *what* from guest */
	auto *buffer = (char *)WS_Alloc(inst.ctx()->ws, g_wlen + 1);
	if (buffer == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	inst.machine().copy_from_guest(buffer, g_what, g_wlen);
	buffer[g_wlen] = 0;

	/* Find the ':' in the buffer */
	const char* colon = strchr(buffer, ':');
	if (colon != nullptr) {
		const size_t namelen = colon - buffer;

		/* Look for a header with an existing name */
		const auto index = http_findhdr(hp, namelen, buffer);
		if (index > 0)
		{
			auto &field = hp->field_array[index];
			field.begin = buffer;
			field.end   = buffer + g_wlen;
			regs.rax = index;
		}
		else /* Not found, append. */
		{
			regs.rax = http_header_append(hp, buffer, g_wlen);
		}
	}
	else {
		/* No colon: Try UNSET (a little bit inefficient) */
		const auto index = http_findhdr(hp, g_wlen, buffer);
		if (index > 0) {
			http_unsetat(hp, index);
			regs.rax = index;
		} else {
			regs.rax = 0;
		}
	}
	/* Return value: Index of header field */
	machine.set_registers(regs);
}

static void syscall_http_find(Machine &machine, MachineInstance &inst)
{
	auto regs = machine.registers();
	const int where = regs.rdi;
	const uint64_t g_what = regs.rsi;
	const uint16_t g_wlen = regs.rdx;

	auto* hp = get_http(inst.ctx(), (gethdr_e)where);

	/* Read out *what* from guest */
	auto* buffer = (char *)WS_Alloc(inst.ctx()->ws, g_wlen + 1);
	if (buffer == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	inst.machine().copy_from_guest(buffer, g_what, g_wlen);
	buffer[g_wlen] = 0;

	/* Find the header field by its name */
	unsigned index = http_findhdr(hp, g_wlen, buffer);
	if (index > 0)
	{
		const uint64_t g_dest    = regs.rcx;
		const uint64_t g_destlen = regs.r8;
		const auto& field = hp->field_array[index];
		const uint64_t flen = field.end - field.begin;
		const uint64_t size = std::min(flen, g_destlen);
		inst.machine().copy_to_guest(g_dest, field.begin, size);
		regs.rax = size;
	} else {
		regs.rax = 0;
	}

	machine.set_registers(regs);
}

} // kvm
