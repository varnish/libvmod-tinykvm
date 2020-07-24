#include "script_functions.hpp"
#include "machine/include_api.hpp"
#include "varnish.hpp"

extern "C" {
	void http_SetH(struct http *to, unsigned n, const char *fm);
	void http_UnsetIdx(struct http *hp, unsigned idx);
	unsigned http_findhdr(const struct http *hp, unsigned l, const char *hdr);
	void HSH_AddString(struct req *, void *ctx, const char *str);
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
}
#define HDR_FIRST     6
#define HDR_INVALID   UINT32_MAX

struct guest_header_field {
	int where;
	uint32_t index;
	bool     deleted = false;
	bool     foreach = false;
};

inline void foreach(http* hp, riscv::Function<void(http*, txt&, size_t)> cb)
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
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (UNLIKELY(hp == nullptr))
		throw std::runtime_error("Selected HTTP not available at this time");

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

inline gaddr_t push_data(machine_t& machine, const char* data, size_t len)
{
	/* Allocate the string on the shared memory area */
	SharedMemoryArea shm {get_script(machine)};
	return shm.push(data, len);
}

/**
 *  These are all system call handlers, which get called by the guest,
 *  and so they have to maintain the integrity of Varnish by checking
 *  everything, and placing sane limits on requests. Additionally,
 *  some system calls are so expensive that they should increment
 *  the guests instruction counter, to make sure that it reflects the
 *  amount of work being done.
**/

APICALL(self_test)
{
	return -1;
}
APICALL(assertion_failed)
{
	auto [expr, file, line, func] =
		machine.sysargs<std::string, std::string, int, std::string> ();
	/* TODO: Use VSLb here */
	fprintf(stderr, ">>> assertion failed: %s in %s:%d, function %s\n",
		expr.c_str(), file.c_str(), line, func.c_str());
	machine.stop();
	return -1; /* Maybe throw? */
}
APICALL(print)
{
	auto [buffer] = machine.sysargs<riscv::Buffer> ();
	auto string = buffer.to_string();
	/* TODO: Use VSLb here or disable this completely */
	printf(">>> %s: %.*s", get_script(machine).name(),
		(int) string.size(), string.c_str());
	return string.size();
}
APICALL(shm_log)
{
	auto [string, len] = machine.sysargs<gaddr_t, uint32_t> ();
	const auto* ctx = get_ctx(machine);

	auto& script = get_script(machine);
	auto* data = script.rw_area.host_addr(string, len);
	if (data) {
		VSLb(ctx->vsl, SLT_VCL_Log, "%.*s", (int) len, data);
		return 0;
	}
	/* Fallback (with potential slow-path) */
	script.machine().memory.memview(string, len,
		[ctx] (const uint8_t* data, size_t len) {
			VSLb(ctx->vsl, SLT_VCL_Log, "%.*s", (int) len, data);
		});
	return 0;
}

APICALL(my_name)
{
	auto& script = get_script(machine);
	/* Put pointer, length in registers A0, A1 */
	const size_t len = __builtin_strlen(script.name());
	machine.cpu.reg(11) = len;
	return push_data(machine, script.name(), len+1); /* Zero-terminated */
}
APICALL(set_decision)
{
	auto [result, status] = machine.sysargs<riscv::Buffer, int> ();
	get_script(machine).set_result(result.to_string(), status);
	return 0;
}
APICALL(ban)
{
	auto [buffer] = machine.sysargs<std::string> ();
	auto* ctx = get_ctx(machine);

	VRT_ban_string(ctx, buffer.c_str());
	return 0;
}
APICALL(hash_data)
{
	auto [buffer] = machine.sysargs<std::string> ();
	auto* ctx = get_ctx(machine);

	HSH_AddString(ctx->req, ctx->specific, buffer.c_str());
	return 0;
}

APICALL(foreach_header_field)
{
	auto [where, func, data] = machine.sysargs<int, gaddr_t, gaddr_t> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	auto& script = get_script(machine);
	/* Push the field struct as well as the string on hidden stack */
	SharedMemoryArea shm {script};

	/* Make room for worst case number of fields */
	const size_t  pcount = hp->field_count - HDR_FIRST;
	gaddr_t field_addr = shm.address() - pcount * sizeof(guest_header_field);

	const gaddr_t first = field_addr;
	SharedMemoryArea shm_fields {script, field_addr};
	int acount = 0;

	/* Iterate each header field */
	for (unsigned idx = HDR_FIRST; idx < hp->field_count; idx++) {
		const auto& field = hp->field_array[idx];
		const uint32_t len = field_length(field);
		/* Ignore empty fields (?) */
		if (len == 0)
			continue;

		shm_fields.write<guest_header_field>(
			{(gethdr_e) where, idx, false, true});
		acount ++; /* Actual */
	}
	/* This will prevent other pushes to interfere with this */
	shm.set(first);

	/* Call into the machine using pre-emption */
	script.preempt((gaddr_t) func, (gaddr_t) data, (gaddr_t) first, (int) acount);

	/* Check if any were deleted */
	int dcount = 0;
	for (int i = 0; i < acount; i++)
	{
		const gaddr_t addr = first + i*sizeof(guest_header_field);
		const bool deleted = machine.memory.read<uint8_t>
			(addr + offsetof(guest_header_field, deleted));
		/* Deleted entries moves everything back */
		if (deleted) {
			const int idx = machine.memory.read<uint32_t>
				(addr + offsetof(guest_header_field, index));
			if (is_valid_index(hp, idx)) {
				http_UnsetIdx(hp, idx - dcount++);
			}
		}
	}

	return acount;
}

APICALL(http_find_name)
{
	const auto [where, fieldname] = machine.sysargs<int, std::string> ();
	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	/* Find the header field by its name */
	unsigned index
		= http_findhdr(hp, fieldname.size(), fieldname.c_str());
	if (index > 0)
		return index;
	/* Not found -> invalid header field */
	return HDR_INVALID;
}
APICALL(http_copy_from)
{
	const auto [where, index, dest]
		= machine.sysargs<int, unsigned, int> ();
	auto* ctx = get_ctx(machine);
	/* Ignore invalid header fields?? */
	if (is_invalid(index))
		return HDR_INVALID;

	auto [hp_from, field_from] = get_field(ctx, (gethdr_e) where, index);
	auto* hp_dest = get_http(ctx, (gethdr_e) dest);

	const size_t len = field_length(field_from);
	/* Avoid overflowing dest */
	if (UNLIKELY(hp_dest->field_count >= hp_dest->fields_max)) {
		VSLb(hp_dest->vsl, SLT_LostHeader,
			"%.*s", (int) len, field_from.begin);
		return -1;
	}

	const int idx_dest = hp_dest->field_count++;
	http_SetH(hp_dest, idx_dest, field_from.begin);
	return idx_dest;
}

APICALL(http_set_status)
{
	const auto [where, status] = machine.sysargs<int, int> ();

	auto* ctx = get_ctx(machine);
	auto [hp, field] = get_field(ctx, where, 3);
	/* Getter does not want to set status */
	if (status < 0)
		return hp->status;
	/* Do the workspace allocation before making changes */
	const char* string = WS_Printf(ctx->ws, "%u", status);
	if (UNLIKELY(!string))
		throw std::runtime_error("Out of workspace");
	hp->status = status;
	/* We have to overwrite the header field too */
	field.begin = string;
	field.end = field.begin + strlen(string);
	return status;
}

APICALL(http_unset_re)
{
	const auto [where, index] = machine.sysargs<int, int> ();
	auto* vre = get_script(machine).regex_get(index);

	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	/* Unset header fields from the top down */
	size_t mcount = 0;
	for (int i = hp->field_count-1; i >= HDR_FIRST; i--)
	{
		auto& field = hp->field_array[i];
		if ( VRE_exec(vre, field.begin, field.end - field.begin,
			0, 0, nullptr, 0, nullptr) >= 0 ) {
			http_UnsetIdx(hp, i);
			mcount ++;
		}
	}
	return mcount;
}

APICALL(http_rollback)
{
	const auto [where] = machine.sysargs<int> ();
	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	VRT_Rollback(ctx, hp);
	return 0;
}

APICALL(header_field_get)
{
	return -1;
}

APICALL(header_field_retrieve)
{
	const auto [where, index, fdata]
		= machine.sysargs<int, uint32_t, gaddr_t> ();

	const auto* hp = get_http(get_ctx(machine), (gethdr_e) where);

	if (is_valid_index(hp, index))
	{
		const auto& field = hp->field_array[index];
		const size_t len = field_length(field);
		auto addr = push_data(machine, field.begin, len+1);
		machine.cpu.reg(11) = addr + len;
		return addr;
	}
	return 0;
}

APICALL(header_field_append)
{
	auto [where, field, len] = machine.sysargs<int, uint32_t, uint32_t> ();
	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	auto* val = (char*) WS_Alloc(ctx->ws, len+1);
	if (val == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	machine.memory.memcpy_out(val, (uint32_t) field, len);
	val[len] = 0;

	if (UNLIKELY(hp->field_count >= hp->fields_max)) {
		VSLb(hp->vsl, SLT_LostHeader, "%.*s", (int) len, val);
		return -1;
	}

	const int idx = hp->field_count++;
	http_SetH(hp, idx, val);
	return idx;
}

APICALL(header_field_set)
{
	auto [where, index, buffer]
		= machine.sysargs<int, unsigned, riscv::Buffer> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	if (is_valid_index(hp, index))
	{
		/* Allocate and extract the new field value */
		auto* val = (char*) WS_Alloc(ctx->ws, buffer.size()+1);
		if (val == nullptr)
			throw std::runtime_error("Unable to make room for HTTP header field");

		buffer.copy_to(val, buffer.size());
		val[buffer.size()] = 0;

		/* Apply it at the given index */
		http_SetH(hp, index, val);
		return buffer.size();
	}
	else if (index == HDR_INVALID) {
		return -1;
	}
	/* This will halt execution */
	throw std::out_of_range("Header field index not in bounds");
}

APICALL(header_field_copy)
{
	auto [where, index, src_where, src_index]
		= machine.sysargs<int, unsigned, int, unsigned> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);
	auto* src_hp = get_http(ctx, (gethdr_e) src_where);

	if (is_valid_index(hp, index))
	{
		if (is_valid_index(src_hp, src_index))
		{
			const auto& src_field = src_hp->field_array[src_index];

			/* Apply it at the given index */
			http_SetH(hp, index, src_field.begin);
			return field_length(src_field);
		}
		else /* In VCL you can unset a header field by assigning it
			to a non-existing other header field. */
		{
			http_UnsetIdx(hp, index);
			return 0;
		}
	}
	else if (index == HDR_INVALID || src_index == HDR_INVALID) {
		return -1;
	}
	/* This will halt execution */
	throw std::out_of_range("Header field index not in bounds");
}

APICALL(header_field_unset)
{
	auto [where, index] = machine.sysargs<int, unsigned> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	/* You are not allowed to unset proto fields */
	if (is_valid_index(hp, index) && index >= HDR_FIRST)
	{
		http_UnsetIdx(hp, index);
		return 0;
	} else if (index == HDR_INVALID) {
		return -1; /* Silently ignored */
	}
	throw std::out_of_range("Header field index not in bounds");
}

APICALL(regex_compile)
{
	auto [pbuffer] = machine.sysargs<riscv::Buffer> ();
	auto pattern = pbuffer.to_string();

	const uint32_t hash = crc32(pattern.c_str(), pattern.size());
	const int idx = get_script(machine).regex_find(hash);
	if (idx >= 0)
		return idx;

	/* Compile new regex pattern */
	const char* error = "";
	int         error_offset = 0;
	auto* re = VRE_compile(pattern.c_str(), 0, &error, &error_offset);
	/* TODO: log errors here */
	if (re == nullptr) {
		printf("Regex compile error: %s\n", error);
		throw std::runtime_error(
			"The regex pattern did not compile: " + pattern);
	}

	return get_script(machine).regex_manage(re, hash);
}
APICALL(regex_match)
{
	auto [index, buffer] = machine.sysargs<uint32_t, riscv::Buffer> ();
	auto* vre = get_script(machine).regex_get(index);
	/* VRE_exec(const vre_t *code, const char *subject, int length,
	    int startoffset, int options, int *ovector, int ovecsize,
	    const volatile struct vre_limits *lim) */
	auto subject = buffer.to_string();
	return VRE_exec(vre, subject.c_str(), subject.size(), 0,
		0, nullptr, 0, nullptr) >= 0;
}
APICALL(regex_subst)
{
	auto [index, tbuffer, sbuffer, dst, maxlen]
		= machine.sysargs<uint32_t, riscv::Buffer, riscv::Buffer, gaddr_t, uint32_t> ();
	auto* re = get_script(machine).regex_get(index);

	/* Run the regsub using existing 're' */
	const bool all = (maxlen & 0x80000000);
	auto subject = tbuffer.to_string();
	auto subst   = sbuffer.to_string();
	auto * result =
		VRT_regsub(get_ctx(machine), all, subject.c_str(), re, subst.c_str());
	if (result == nullptr)
		return -1;

	/* This call only supports dest buffer being in the RW area */
	const size_t len =
		std::min((size_t) maxlen & 0x7FFFFFFF, __builtin_strlen(result)+1);
	machine.copy_to_guest(dst, result, len);
	return len-1; /* The last byte is the zero, not reporting that */
}
APICALL(regex_subst_hdr)
{
	auto [ridx, where, index, subst, all]
		= machine.sysargs<uint32_t, int, uint32_t, std::string, int> ();
	auto* re = get_script(machine).regex_get(ridx);
	auto* ctx = get_ctx(machine);
	if (index == HDR_INVALID)
		return -1;
	auto [hp, field] = get_field(ctx, (gethdr_e) where, index);

	/* Run the regsub using existing 're' */
	auto * result =
		VRT_regsub(ctx, all, field.begin, re, subst.c_str());
	if (result == nullptr)
		return -1;

	http_SetH(hp, index, result);
	return __builtin_strlen(result);
}
APICALL(regex_delete)
{
	auto [index] = machine.sysargs<uint32_t> ();
	get_script(machine).regex_free((uint32_t) index);
	return 0;
}

void Script::setup_syscall_interface(machine_t& machine)
{
	machine.install_syscall_handlers({
		{ECALL_SELF_TEST,   self_test},
		{ECALL_ASSERT_FAIL, assertion_failed},
		{ECALL_PRINT,       print},
		{ECALL_LOG,         shm_log},

		{ECALL_MY_NAME,      my_name},
		{ECALL_SET_DECISION, set_decision},
		{ECALL_BAN,          ban},
		{ECALL_HASH_DATA,    hash_data},

		{ECALL_REGEX_COMPILE, regex_compile},
		{ECALL_REGEX_MATCH,   regex_match},
		{ECALL_REGEX_SUBST,   regex_subst},
		{ECALL_REGSUB_HDR,    regex_subst_hdr},
		{ECALL_REGEX_FREE,    regex_delete},

		{ECALL_FOREACH_FIELD, foreach_header_field},
		{ECALL_FIELD_GET,     header_field_get},
		{ECALL_FIELD_RETRIEVE,header_field_retrieve},
		{ECALL_FIELD_APPEND,  header_field_append},
		{ECALL_FIELD_SET,     header_field_set},
		{ECALL_FIELD_COPY,    header_field_copy},
		{ECALL_FIELD_UNSET,   header_field_unset},

		{ECALL_HTTP_SET_STATUS, http_set_status},
		{ECALL_HTTP_UNSET_RE,   http_unset_re},
		{ECALL_HTTP_FIND,       http_find_name},
		{ECALL_HTTP_COPY,       http_copy_from},
		{ECALL_HTTP_ROLLBACK,   http_rollback},
	});
}
