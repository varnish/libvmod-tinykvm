#include "script_functions.hpp"
#include "machine/include_api.hpp"
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

inline std::tuple<http*, txt&>
get_field(VRT_CTX, int where, uint32_t index)
{
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

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

inline uint32_t push_field(SharedMemoryArea& shm,
	const txt& field, int where, uint32_t idx)
{
	const uint32_t len = field_length(field);
	const uint32_t sh_data = shm.push(field.begin, len + 1);
	return shm.push<guest_header_field>(
		{where, idx, sh_data, sh_data + len});
}

APICALL(foreach_header_field)
{
	auto [where, func, data] = machine.sysargs<int, gaddr_t, gaddr_t> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = VRT_selecthttp(ctx, (gethdr_e) where);
	if (hp == nullptr)
		throw std::runtime_error("Selected HTTP not available at this time");

	/* Iterate each header field */
	for (unsigned i = 6; i < hp->field_count; i++) {
		const auto& field = hp->field_array[i];
		const uint32_t len = field_length(field);
		/* Ignore empty fields (?) */
		if (len == 0)
			continue;

		auto& script = get_script(machine);
		/* Push the field struct as well as the string on hidden stack */
		SharedMemoryArea shm {script};
		auto sh_hdr = push_field(shm, field, where, i);
		/* Call into the machine using pre-emption */
		script.preempt((gaddr_t) func, (gaddr_t) data, (gaddr_t) sh_hdr);
		/* Stop iterating if it crashed during the callback */
		if (script.crashed())
			return -1;
		const bool deleted =
			machine.memory.read<uint8_t> (sh_hdr + offsetof(guest_header_field, deleted));
		/* Deleted entries moves everything back */
		if (deleted) {
			if (http_UnsetIdx(hp, i) < 0)
				throw std::runtime_error("Unable to unset entry at " + std::to_string(i));
			i --;
		}
	}

	return 0;
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

	const char* error = "";
	int         error_offset = 0;
	auto* re = VRE_compile(pattern.c_str(), 0, &error, &error_offset);
	/* TODO: log errors here */
	if (re == nullptr) {
		printf("Regex compile error: %s\n", error);
		throw std::runtime_error("The regex pattern did not compile: " + pattern);
	}
	return get_script(machine).regex_manage(re);
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

		{ECALL_FOREACH_FIELD, foreach_header_field},
		{ECALL_FIELD_GET_L,   header_field_get_length},
		{ECALL_FIELD_GET,     header_field_get},
		{ECALL_FIELD_APPEND,  header_field_append},
		{ECALL_FIELD_SET,     header_field_set},
		{ECALL_FIELD_UNSET,   header_field_unset},

		{ECALL_REGEX_COMPILE, regex_compile},
		{ECALL_REGEX_MATCH,   regex_match},
		{ECALL_REGEX_SUBST,   regex_subst},
		{ECALL_REGEX_FREE,    regex_delete},
	});
}
