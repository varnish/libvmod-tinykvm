#include "script_functions.hpp"
#include "machine/include_api.hpp"
#include "crc32.hpp"
#include "shm.hpp"
using gaddr_t = uint32_t;

extern "C" {
# include <vdef.h>
# include <vrt.h>
# include <vre.h>
	void http_SetHeader(struct http *to, const char *hdr);
	void http_SetH(struct http *to, unsigned n, const char *fm);
	int  http_UnsetIdx(struct http *hp, unsigned idx);
	void *WS_Copy(struct ws *ws, const void *str, int len);
	void *WS_Alloc(struct ws *ws, unsigned bytes);
	char *WS_Printf(struct ws *ws, const char *fmt, ...);
	typedef struct {
		const char* begin;
		const char* end;
	} txt;
	struct http {
		unsigned       magic;
		uint16_t       fields_max;
		txt*           field_array;
		unsigned char* field_flags;
		uint16_t       field_count;
		int some_shit;
		void*          vsl;
		void*          ws;
		uint16_t       status;
		uint8_t        protover;
		uint8_t        conds;
	};
}
typedef struct {
	int where;
	uint32_t index;
	uint32_t begin;
	uint32_t end;
	bool     deleted;
} guest_header_field;

inline bool is_valid_index(const http* hp, unsigned idx) {
	return idx >= 1 && idx < hp->field_count;
}

inline http*
get_http(VRT_CTX, int where)
{
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
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

APICALL(self_test)
{
	return -1;
}
APICALL(assertion_failed)
{
	auto [expr, file, line, func] =
		machine.sysargs<std::string, std::string, int, std::string> ();
	fprintf(stderr, ">>> assertion failed: %s in %s:%d, function %s\n",
		expr.c_str(), file.c_str(), line, func.c_str());
	machine.stop();
	return -1;
}
APICALL(print)
{
	auto [address, len] = machine.sysargs<gaddr_t, uint32_t> ();
	const uint32_t len_g = std::min(1024u, (uint32_t) len);
	machine.memory.memview(address, len_g,
		[&machine] (const uint8_t* data, size_t len) {
			if (data == nullptr) {
				printf(">>> Program attempted an illegal write\n");
				return;
			}
			printf(">>> Program says: %.*s", (int) len, data);
		});
	return len_g;
}

APICALL(foreach_header_field)
{
	auto [where, func, data] = machine.sysargs<int, gaddr_t, gaddr_t> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

	auto& script = get_script(machine);
	/* Push the field struct as well as the string on hidden stack */
	SharedMemoryArea shm {script};

	/* Make room for worst case number of fields */
	const size_t  pcount = hp->field_count - 6;
	gaddr_t data_addr = shm.address() - pcount * sizeof(guest_header_field);
	gaddr_t field_addr = data_addr;

	const gaddr_t first = field_addr;
	SharedMemoryArea shm_fields {script, field_addr};
	SharedMemoryArea shm_data   {script, data_addr};
	int acount = 0;

	/* Iterate each header field */
	for (unsigned idx = 6; idx < hp->field_count; idx++) {
		const auto& field = hp->field_array[idx];
		const uint32_t len = field_length(field);
		/* Ignore empty fields (?) */
		if (len == 0)
			continue;

		const uint32_t sh_data = shm_data.push(field.begin, len + 1);
		shm_fields.write<guest_header_field>(
			{(gethdr_e) where, idx, sh_data, sh_data + len});
		acount ++; /* Actual */
	}

	/* This will prevent other pushes to interfere with this */
	shm.set(shm_data.address());

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
			if (http_UnsetIdx(hp, idx - dcount++) < 0)
				throw std::runtime_error("Unable to unset entry at " + std::to_string(idx));
		}
	}

	return acount;
}
APICALL(http_set_status)
{
	const auto [where, status] = machine.sysargs<int, int> ();

	auto* ctx = get_ctx(machine);
	auto [hp, field] = get_field(ctx, where, 3);
	/* Getter does not want to set status */
	if (status < 0)
		return hp->status;
	hp->status = status;
	/* We have to overwrite the header field too */
	field.begin = WS_Printf(ctx->ws, "%u", status);
	field.end = field.begin + strlen(field.begin);
	return status;
}


APICALL(header_field_get_length)
{
	const auto [where, index]
		= machine.sysargs<int, int> ();
	const auto [hp, field] = get_field(get_ctx(machine), where, index);

	return field_length(field);
}

APICALL(header_field_get)
{
	auto [where, index, fdata, sdata, sdatalen]
		= machine.sysargs<int, uint32_t, gaddr_t, gaddr_t, uint32_t> ();

	auto [hp, field] = get_field(get_ctx(machine), where, index);
	const uint32_t len = field_length(field);
	if (len >= sdatalen || field.begin == nullptr)
		return -1;

	const guest_header_field hf {where, index, sdata, sdata + len};
	machine.copy_to_guest(fdata, &hf, sizeof(hf));
	machine.copy_to_guest(sdata, field.begin, len+1);
	return len;
}

APICALL(header_field_append)
{
	auto [where, field, len] = machine.sysargs<int, uint32_t, uint32_t> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

	auto* val = (char*) WS_Alloc(ctx->ws, len+1);
	if (val == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	machine.memory.memcpy_out(val, (uint32_t) field, len);
	val[len] = 0;
	http_SetHeader(hp, val);
	return 0;
}

APICALL(header_field_set)
{
	auto [where, index, src, len]
		= machine.sysargs<int, int, uint32_t, uint32_t> ();
	if (len > 0x40000000)
		throw std::runtime_error("Invalid length");

	const auto* ctx = get_ctx(machine);
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

	if (is_valid_index(hp, index))
	{
		/* Allocate and extract the new field value */
		auto* val = (char*) WS_Alloc(ctx->ws, len+1);
		if (val == nullptr)
			throw std::runtime_error("Unable to make room for HTTP header field");
		machine.memory.memcpy_out(val, (uint32_t) src, len);
		val[len] = 0;

		/* Apply it at the given index */
		http_SetH(hp, index, val);
		return len;
	}
	return -1;
}

APICALL(header_field_unset)
{
	auto [where, index] = machine.sysargs<int, int> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

	if (is_valid_index(hp, index))
	{
		return http_UnsetIdx(hp, index);
	}
	return -1;
}

APICALL(regex_compile)
{
	auto [pattern] = machine.sysargs<std::string> ();

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
		throw std::runtime_error("The regex pattern did not compile: " + pattern);
	}

	return get_script(machine).regex_manage(re, hash);
}
APICALL(regex_match)
{
	auto [index, subject] = machine.sysargs<uint32_t, std::string> ();
	auto* vre = get_script(machine).regex_get(index);
	/* VRE_exec(const vre_t *code, const char *subject, int length,
	    int startoffset, int options, int *ovector, int ovecsize,
	    const volatile struct vre_limits *lim) */
	return VRE_exec(vre, subject.c_str(), subject.size(), 0,
		0, nullptr, 0, nullptr);
}
APICALL(regex_subst)
{
	auto [index, all, subject, subst, dst, maxlen]
		= machine.sysargs<uint32_t, int, std::string, std::string, gaddr_t, uint32_t> ();
	auto* re = get_script(machine).regex_get(index);

	auto* result =
		VRT_regsub(get_ctx(machine), all, subject.c_str(), re, subst.c_str());
	if (result == nullptr)
		return -1;
	const size_t len = std::min((size_t) maxlen, __builtin_strlen(result)+1);
	machine.copy_to_guest((gaddr_t) dst, result, len);
	return len-1; /* The last byte is the zero, not reporting that */
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

		{ECALL_REGEX_COMPILE, regex_compile},
		{ECALL_REGEX_MATCH,   regex_match},
		{ECALL_REGEX_SUBST,   regex_subst},
		{ECALL_REGEX_FREE,    regex_delete},

		{ECALL_FOREACH_FIELD, foreach_header_field},
		{ECALL_FIELD_GET_L,   header_field_get_length},
		{ECALL_FIELD_GET,     header_field_get},
		{ECALL_FIELD_APPEND,  header_field_append},
		{ECALL_FIELD_SET,     header_field_set},
		{ECALL_FIELD_UNSET,   header_field_unset},

		{ECALL_HTTP_SET_STATUS, http_set_status},
	});
}
