#include "script_functions.hpp"
#include "machine/include_api.hpp"
#include "varnish.hpp"

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
extern "C" {
	void http_SetH(struct http *to, unsigned n, const char *fm);
	void http_UnsetIdx(struct http *hp, unsigned idx);
	unsigned http_findhdr(const struct http *hp, unsigned l, const char *hdr);
	void http_PrintfHeader(struct http *to, const char *fmt, ...);
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

inline void foreach(http* hp, const riscv::Function<void(http*, txt&, size_t)>& cb)
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

APICALL(fail)
{
	auto [buffer] = machine.sysargs<riscv::Buffer> ();
	machine.stop();
	throw std::runtime_error("Assertion failed: " + buffer.to_string());
}
APICALL(assertion_failed)
{
	auto [expr, file, line, func] =
		machine.sysargs<std::string, std::string, int, std::string> ();
	/* TODO: Use VSLb here */
	fprintf(stderr, ">>> assertion failed: %s in %s:%d, function %s\n",
		expr.c_str(), file.c_str(), line, func.c_str());
	machine.stop();
	throw std::runtime_error("Assertion failed");
}
APICALL(print)
{
	auto [buffer] = machine.sysargs<riscv::Buffer> ();
	auto string = buffer.to_string();
	/* TODO: Use VSLb here or disable this completely */
	printf(">>> %s: %.*s\n", get_script(machine).name().c_str(),
		(int) string.size(), string.c_str());
	machine.set_result(string.size());
}
APICALL(shm_log)
{
	auto [string, len] = machine.sysargs<gaddr_t, uint32_t> ();
	const auto* ctx = get_ctx(machine);

	auto& script = get_script(machine);
	auto* data = script.rw_area.host_addr(string, len);
	if (data) {
		VSLb(ctx->vsl, SLT_VCL_Log, "%.*s", (int) len, data);
		machine.set_result(len);
		return;
	}
	/* Fallback (with potential slow-path) */
	auto buffer = machine.memory.rvbuffer(string, len);
	if (buffer.is_sequential()) {
		VSLb(ctx->vsl, SLT_VCL_Log, "%.*s", (int) buffer.size(), buffer.c_str());
		machine.set_result(len);
		return;
	}
	// TODO: slow-path
	machine.set_result(-1);
}
APICALL(dynamic_call)
{
	auto [hash] = machine.template sysargs <uint32_t> ();
	auto& regs = machine.cpu.registers();
	// move down 6 integer registers
	for (int i = 0; i < 6; i++) {
		regs.get(10 + i) = regs.get(11 + i);
	}
	// call the handler
	get_script(machine).dynamic_call(hash);
	// we short-circuit the ret pseudo-instruction:
	machine.cpu.jump(regs.get(riscv::RISCV::REG_RA) - 4);
}

APICALL(my_name)
{
	auto& script = get_script(machine);
	const auto& name = script.name();
	/* Put pointer, length in registers A0, A1 */
	machine.cpu.reg(11) = name.size();
	machine.set_result( /* NOTE: Zero-terminated */
		push_data(machine, name.c_str(), name.size()+1));
}
APICALL(set_decision)
{
	auto [result, status, paused] =
		machine.sysargs<riscv::Buffer, gaddr_t, bool> ();
	auto& script = get_script(machine);
	if (result.is_sequential()) {
		script.set_result(result.c_str(), status, paused);
	} else {
		script.set_result(result.to_string(), status, paused);
	}
	machine.stop();
}
APICALL(backend_decision)
{
	auto [func, farg] =
		machine.sysargs<gaddr_t, gaddr_t> ();
	auto& script = get_script(machine);
	script.set_results("backend", {func, farg});
	machine.stop();
}
APICALL(ban)
{
	auto [buffer] = machine.sysargs<std::string> ();
	auto* ctx = get_ctx(machine);

	VRT_ban_string(ctx, buffer.c_str());
}
APICALL(hash_data)
{
	auto [buffer] = machine.sysargs<riscv::Buffer> ();
	auto& script = get_script(machine);

	if (buffer.is_sequential())
		script.hash_buffer(buffer.c_str(), buffer.size());
	else
		script.hash_buffer(buffer.to_string().c_str(), buffer.size());
}
APICALL(purge)
{
	machine.set_result(-1);
}

APICALL(synth)
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	const auto* ctx = get_ctx(machine);
	if (ctx->method == VCL_MET_SYNTH ||
		ctx->method == VCL_MET_BACKEND_ERROR)
	{
		auto* hp = get_http(ctx, HDR_RESP);
		const auto [type, data] = machine.sysargs<riscv::Buffer, riscv::Buffer> ();
		http_PrintfHeader(hp, "Content-Length: %u", data.size());
		http_PrintfHeader(hp, "Content-Type: %.*s", (int) type.size(), type.to_string().c_str());

		auto* vsb = (struct vsb*) ctx->specific;
		assert(vsb != nullptr);

		if (data.is_sequential()) {
			/* we need to get rid of the old data */
			if (vsb->s_flags & VSB_DYNAMIC) {
				free(vsb->s_buf);
			}
			vsb->s_buf = (char*) data.c_str();
			vsb->s_size = data.size() + 1; /* pretend-zero */
			vsb->s_len  = data.size();
			vsb->s_flags = VSB_FIXEDLEN;
		} else {
			VSB_clear(vsb);
			VSB_bcat(vsb, data.to_string().c_str(), data.size());
		}
		//VSB_finish(vsb);
		machine.stop();
#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent in synth syscall: %ld ns\n", nanodiff(t0, t1));
#endif
		return;
	}
	throw std::runtime_error(
	    "Synth can only be used in vcl_synth or vcl_backend_error");
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

	machine.set_result(acount);
}

APICALL(http_find_name)
{
	const auto [where, fieldname] = machine.sysargs<int, riscv::Buffer> ();
	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	if (fieldname.is_sequential())
	{
		/* Find the header field by its name */
		unsigned index
			= http_findhdr(hp, fieldname.size(), fieldname.c_str());
		if (index > 0) {
			machine.set_result(index);
			return;
		}
	} else {
		/* Find the header field by its name */
		unsigned index
			= http_findhdr(hp, fieldname.size(), fieldname.to_string().c_str());
		if (index > 0) {
			machine.set_result(index);
			return;
		}
	}
	/* Not found -> invalid header field */
	machine.set_result(HDR_INVALID);
}
APICALL(http_copy_from)
{
	const auto [where, index, dest]
		= machine.sysargs<int, unsigned, int> ();
	auto* ctx = get_ctx(machine);
	/* Ignore invalid header fields?? */
	if (is_invalid(index)) {
		machine.set_result(HDR_INVALID);
		return;
	}

	auto [hp_from, field_from] = get_field(ctx, (gethdr_e) where, index);
	auto* hp_dest = get_http(ctx, (gethdr_e) dest);

	const size_t len = field_length(field_from);
	/* Avoid overflowing dest */
	if (UNLIKELY(hp_dest->field_count >= hp_dest->fields_max)) {
		VSLb(hp_dest->vsl, SLT_LostHeader,
			"%.*s", (int) len, field_from.begin);
		machine.set_result(HDR_INVALID);
		return;
	}

	const int idx_dest = hp_dest->field_count++;
	http_SetH(hp_dest, idx_dest, field_from.begin);
	machine.set_result(idx_dest);
}

APICALL(http_set_status)
{
	const auto [where, status] = machine.sysargs<int, int> ();

	auto* ctx = get_ctx(machine);
	auto [hp, field] = get_field(ctx, where, 3);
	/* Getter does not want to set status */
	if (status < 0) {
		machine.set_result(hp->status);
		return;
	}
	/* Do the workspace allocation before making changes */
	const char* string = WS_Printf(ctx->ws, "%u", status);
	if (UNLIKELY(!string))
		throw std::runtime_error("Out of workspace");
	hp->status = status;
	/* We have to overwrite the header field too */
	field.begin = string;
	field.end = field.begin + strlen(string);
	machine.set_result(status);
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
	machine.set_result(mcount);
}

APICALL(http_rollback)
{
	const auto [where] = machine.sysargs<int> ();
	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	VRT_Rollback(ctx, hp);
}

APICALL(header_field_get)
{
	machine.set_result(-1);
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
		/* Return begin, end pointer */
		machine.set_result(addr, addr + len);
		return;
	}
	/* Null pointers */
	machine.set_result(0, 0);
}

APICALL(header_field_append)
{
	auto [where, field, len] = machine.sysargs<int, gaddr_t, uint32_t> ();
	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	auto* val = (char*) WS_Alloc(ctx->ws, len+1);
	if (val == nullptr)
		throw std::runtime_error("Unable to make room for HTTP header field");
	machine.memory.memcpy_out(val, (gaddr_t) field, len);
	val[len] = 0;

	if (UNLIKELY(hp->field_count >= hp->fields_max)) {
		VSLb(hp->vsl, SLT_LostHeader, "%.*s", (int) len, val);
		machine.set_result(HDR_INVALID);
		return;
	}

	const int idx = hp->field_count++;
	http_SetH(hp, idx, val);
	machine.set_result(idx);
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
		machine.set_result(buffer.size());
		return;
	}
	else if (index == HDR_INVALID) {
		machine.set_result(-1);
		return;
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
			machine.set_result(field_length(src_field));
			return;
		}
		else /* In VCL you can unset a header field by assigning it
			to a non-existing other header field. */
		{
			http_UnsetIdx(hp, index);
			machine.set_result(HDR_INVALID);
			return;
		}
	}
	else if (index == HDR_INVALID || src_index == HDR_INVALID) {
		machine.set_result(HDR_INVALID);
		return;
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
		machine.set_result(index);
		return;
	} else if (index == HDR_INVALID) {
		/* Silently ignored */
		machine.set_result(HDR_INVALID);
		return;
	}
	throw std::out_of_range("Header field index not in bounds");
}

APICALL(regex_compile)
{
	auto [pbuffer] = machine.sysargs<riscv::Buffer> ();
	auto pattern = pbuffer.to_string();

	const uint32_t hash = crc32(pattern.c_str(), pattern.size());
	const int idx = get_script(machine).regex_find(hash);
	if (idx >= 0) {
		machine.set_result(idx);
		return;
	}

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
	/* Return the regex handle */
	machine.set_result(
		get_script(machine).regex_manage(re, hash));
}
APICALL(regex_match)
{
	auto [index, buffer] = machine.sysargs<uint32_t, riscv::Buffer> ();
	auto* vre = get_script(machine).regex_get(index);
	/* VRE_exec(const vre_t *code, const char *subject, int length,
	    int startoffset, int options, int *ovector, int ovecsize,
	    const volatile struct vre_limits *lim) */
	auto subject = buffer.to_string();
	machine.set_result(
		VRE_exec(vre, subject.c_str(), subject.size(), 0,
		0, nullptr, 0, nullptr) >= 0);
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
	if (result == nullptr) {
		machine.set_result(-1);
		return;
	}

	/* This call only supports dest buffer being in the RW area */
	const size_t len =
		std::min((size_t) maxlen & 0x7FFFFFFF, __builtin_strlen(result)+1);
	machine.copy_to_guest(dst, result, len);
	/* The last byte is the zero, not reporting that */
	machine.set_result(len-1);
}
APICALL(regex_subst_hdr)
{
	auto [ridx, where, index, subst, all]
		= machine.sysargs<uint32_t, int, uint32_t, riscv::Buffer, int> ();
	auto* re = get_script(machine).regex_get(ridx);
	auto* ctx = get_ctx(machine);
	if (index == HDR_INVALID) {
		machine.set_result(-1);
		return;
	}
	auto [hp, field] = get_field(ctx, (gethdr_e) where, index);

	const char* result = nullptr;

	/* Run the regsub using existing 're' */
	if (subst.is_sequential()) {
		result = VRT_regsub(ctx, all, field.begin, re, subst.c_str());
	} else {
		result = VRT_regsub(ctx, all, field.begin, re, subst.to_string().c_str());
	}
	if (result == nullptr) {
		machine.set_result(-1);
		return;
	}

	http_SetH(hp, index, result);
	machine.set_result(__builtin_strlen(result));
}
APICALL(regex_delete)
{
	auto [index] = machine.sysargs<uint32_t> ();
	get_script(machine).regex_free((uint32_t) index);
}

void Script::setup_syscall_interface(machine_t& machine)
{
	#define FPTR(x) machine_t::syscall_fptr_t { x }
	#ifdef __GNUG__
	#define FuckOff
	#else
	#define FuckOff constinit
	#endif
	static FuckOff std::array<const machine_t::syscall_t, ECALL_LAST - SYSCALL_BASE> handlers {
		FPTR(fail),
		FPTR(assertion_failed),
		FPTR(print),
		FPTR(shm_log),
		FPTR(::dynamic_call),

		FPTR(regex_compile),
		FPTR(regex_match),
		FPTR(regex_subst),
		FPTR(regex_subst_hdr),
		FPTR(regex_delete),

		FPTR(my_name),
		FPTR(set_decision),
		FPTR(backend_decision),
		FPTR(ban),
		FPTR(hash_data),
		FPTR(purge),
		FPTR(synth),

		FPTR(foreach_header_field),
		FPTR(header_field_get),
		FPTR(header_field_retrieve),
		FPTR(header_field_append),
		FPTR(header_field_set),
		FPTR(header_field_copy),
		FPTR(header_field_unset),

		FPTR(http_rollback),
		FPTR(http_copy_from),
		FPTR(http_set_status),
		FPTR(http_unset_re),
		FPTR(http_find_name)
	};
	machine.install_syscall_handler_range(SYSCALL_BASE, handlers);
}

timespec time_now()
{
	timespec t;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t;
}
long nanodiff(timespec start_time, timespec end_time)
{
	assert(end_time.tv_sec == 0); /* We should never use seconds */
	return end_time.tv_nsec - start_time.tv_nsec;
}
