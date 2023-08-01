#include "script_functions.hpp"
#include "machine/include_api.hpp"
#include "machine_instance.hpp"
#include "varnish.hpp"
extern "C" {
# include "varnish_interface.h"
}

namespace rvs {
	static const uint64_t REMOTE_CALL_COST = 4000;
	static const uint16_t DEBUG_PORT = 2159;

//#define ENABLE_TIMING
#ifdef ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");
static timespec time_now();
static long nanodiff(timespec start_time, timespec end_time);
#endif

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
	const auto [buffer] = machine.sysargs<riscv::Buffer> ();
	auto& script = get_script(machine);
	if (buffer.is_sequential()) {
		script.print({buffer.c_str(), buffer.size()});
	} else {
		script.print(buffer.to_string());
	}
	machine.set_result(buffer.size());
}
APICALL(shm_log)
{
	auto [string, len] = machine.sysargs<gaddr_t, uint32_t> ();
	const auto* ctx = get_ctx(machine);

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
APICALL(breakpoint)
{
	auto& script = get_script(machine);
	auto* ctx = script.ctx();
	if (script.is_debug()) {
		VSLb(ctx->vsl, SLT_Debug,
			"VM breakpoint at 0x%lX", (long) machine.cpu.pc());
		script.open_debugger(DEBUG_PORT);
	} else {
		VSLb(ctx->vsl, SLT_Debug,
			"Skipped VM breakpoint at 0x%lX (debug not enabled)",
			(long) machine.cpu.pc());
	}
}
APICALL(signal)
{
	auto [sig, handler] = machine.sysargs<int, gaddr_t> ();
	get_script(machine).set_sigaction(sig, handler);
}
APICALL(remote_call)
{
	auto [func] = machine.sysargs <gaddr_t> ();
	auto& script = get_script(machine);
	auto& instance = script.program();
	auto& remote = instance.storage;
	// Get machine registers
	auto& myregs = machine.cpu.registers();
	auto& stregs = remote.machine().cpu.registers();

	// === Serialized access to storage === //
	std::scoped_lock lock(instance.storage_mtx);
	for (int i = 0; i < 4; i++) {
		// Integer registers
		stregs.get(10 + i) = myregs.get(11 + i);
	}
	// Page-sharing mechanisms
	remote.machine().memory.set_page_readf_handler(
		[&m = machine.memory] (const auto&, size_t pageno) -> const auto& {
			// This works because the default return value for
			// missing pages is a CoW zero-page (in both machines).
			return m.get_pageno(pageno);
		});
	remote.machine().memory.set_page_fault_handler(
		[&s = script] (auto& mem, const size_t pageno, bool) -> auto& {
			const gaddr_t addr = pageno * riscv::Page::size();
			if (s.within_heap(addr) || s.within_stack(addr)) {
				auto& pg = s.machine().memory.create_writable_pageno(pageno);
				// The page is created on the source Machine, but we need
				// to invalidate the page on the remote Machine too:
				mem.invalidate_cache(pageno, &pg);
				return pg;
			}
			// Check memory limits before allocating new page
			if (LIKELY(mem.pages_active() < s.max_memory() / riscv::Page::size())) {
				return mem.allocate_page(pageno);
			}
			throw riscv::MachineException(riscv::OUT_OF_MEMORY,
				"Out of memory", s.max_memory());
		});
	// Reset instruction counter for measuring purposes
	remote.machine().reset_instruction_counter();
	// Make storage VM function call
	remote.call(func);
	// Accumulate instruction counting from the remote machine
	// plus some extra because remote calls are expensive
	machine.increment_counter(REMOTE_CALL_COST
		+ remote.machine().instruction_counter());

	for (int i = 0; i < 4; i++) {
		myregs.get(10 + i) = stregs.get(10 + i);
	}
	remote.machine().memory.reset_page_readf_handler();
	// Short-circuit the ret pseudo-instruction:
	machine.cpu.jump(machine.cpu.reg(riscv::REG_RA) - 4);
}
APICALL(remote_strcall)
{
	auto [tramp, func, data, len] =
		machine.sysargs <gaddr_t, gaddr_t, gaddr_t, int> ();
	//printf("Remote stringcall: tramp=0x%X func=0x%X data=0x%X len=%d\n",
	//	tramp, func, data, len);
	auto& script = get_script(machine);
	auto& instance = script.program();
	auto& remote = instance.storage;

	// All arguments have to be valid, and we
	// can't call this from the storage machine
	if (script.is_storage() || tramp == 0 || data == 0 || len < 0) {
		gaddr_t resdata = script.guest_alloc(1);
		script.machine().memory.write<uint8_t> (resdata, 0);
		machine.set_result(resdata, 0);
		return;
	}

	// === Serialized access to storage === //
	std::scoped_lock lock(instance.storage_mtx);

	gaddr_t gaddr = remote.guest_alloc(len+1);
	if (gaddr == 0) {
		throw riscv::MachineException(riscv::OUT_OF_MEMORY,
			"Remote call: Remote machine out of memory");
	}
	// copy the string to the remote machine
	remote.machine().memory.memcpy(gaddr, machine, data, len+1);

	// Storage VM function call
	remote.machine().reset_instruction_counter();
	remote.call((gaddr_t) tramp, (gaddr_t) func, (gaddr_t) gaddr, (int) len);

	// copy a result string back
	const auto [raddr, rlen] = remote.machine().sysargs<gaddr_t, unsigned> ();
	if (raddr == 0) {
		throw std::runtime_error(
			"Null-pointer returned from remote during remote call");
	}

	// copy over the data returned from storage
	const gaddr_t resdata = script.guest_alloc(rlen+1);
	if (resdata == 0) {
		throw riscv::MachineException(riscv::OUT_OF_MEMORY,
			"Remote call: Tenant machine out of memory");
	}
	machine.memory.memcpy(resdata, remote.machine(), raddr, rlen+1);
	// set result now, even if there's an exception in storage
	// we have already gotten our result
	machine.set_result(resdata, rlen);

	// Finish up: let the guest destroy stuff on its own
	// We set aside a few thousand instructions for cleanup
	// NOTE: They will still count against the totals
	remote.resume(64000);
	// free string passed to storage
	remote.guest_free(gaddr);
	//printf("Resume completed  gaddr=0x%X  raddr=0x%X  instr=%zu\n",
	//	gaddr, raddr, remote.machine().instruction_counter());
	// accumulate instruction counting from the remote machine
	// plus some extra because remote calls are expensive
	machine.increment_counter(REMOTE_CALL_COST + remote.machine().instruction_counter());
}

APICALL(register_callback)
{
	const auto [idx, addr] =
		machine.sysargs<uint32_t, gaddr_t>();
	auto& script = get_script(machine);
	script.program().callback_entries.at(idx) = addr;
	machine.set_result(0);
}
APICALL(set_decision)
{
	if (machine.is_forked())
	{
		// A forked VM in any VCL stage
		auto [result, status, paused] =
			machine.sysargs<riscv::Buffer, gaddr_t, bool> ();
		auto& script = get_script(machine);
		script.set_result(result.to_string(), status, paused);
	} else {
		// An initializing tenant VM
		auto [on_recv] = machine.sysargs<gaddr_t> ();
		// Set the tenant as paused
		auto& script = get_script(machine);
		script.pause();
		// Overwrite the on_recv function, if set
		if (on_recv != 0x0) {
			auto& inst = script.program();
			inst.callback_entries.at(1) = on_recv;
		}
	}
	machine.stop();
}
APICALL(set_backend)
{
	auto [be] = machine.sysargs<int> ();
	auto& script = get_script(machine);
	auto* dir = script.directors().get(be);
	machine.set_result(riscv_SetBackend(script.ctx(), dir));
}
APICALL(backend_decision)
{
	auto [caching, func, farg, farglen] =
		machine.sysargs<unsigned, gaddr_t, gaddr_t, gaddr_t> ();
	auto& script = get_script(machine);
	if (func != 0x0) {
		script.set_results("backend", {caching, func, farg}, true);
	} else {
		script.set_results("compute", {caching, farg, farglen}, true);
	}
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
		// Synth responses are always using HDR_RESP
		auto* hp = get_http(ctx, HDR_RESP);
		const auto [status, type, data] = machine.sysargs<uint16_t, riscv::Buffer, riscv::Buffer> ();
		if (UNLIKELY(status < 100))
			throw std::runtime_error("Invalid synth status code: " + std::to_string(status));

		http_SetStatus(hp, status);
		http_PrintfHeader(hp, "Content-Length: %u", data.size());
		if (type.is_sequential())
			http_PrintfHeader(hp, "Content-Type: %.*s", (int)type.size(), type.data());
		else
			http_PrintfHeader(hp, "Content-Type: %.*s", (int)type.size(), type.to_string().c_str());

		auto* vsb = (struct vsb *)ctx->specific;
		assert(vsb != nullptr);

		// riscv::Buffer tries to accumulate the data in a way
		// that preserves continuity even if it crosses many pages.
		// Sadly, even if just one data range is different, we need
		// to sequentialize it.
		if (data.is_sequential()) {
			// Delete any old heap-allocated data
			if (vsb->s_flags & VSB_DYNAMIC) {
				free(vsb->s_buf);
			}
			// Make VSB fixed length, which does not call free
			// XXX: Varnish will literally append a \0 at len.
			vsb->s_buf = (char *)data.c_str();
			vsb->s_size = data.size() + 1; /* pretend-zero */
			vsb->s_len  = data.size();
			vsb->s_flags = VSB_FIXEDLEN;
		} else {
			// By using realloc we do not need to delete old data,
			// although only if s_flags is *not* VSB_FIXEDLEN.
			// We allocate one more byte for the VCP-appended zero.
			char* new_buffer = nullptr;
			if (vsb->s_flags & VSB_DYNAMIC) {
				new_buffer = (char *)realloc(vsb->s_buf, data.size()+1);
			} else if (vsb->s_flags & VSB_FIXEDLEN) {
				new_buffer = (char *)malloc(data.size()+1);
			} else {
				throw std::runtime_error("Unable to determine type of synth content buffer");
			}
			if (UNLIKELY(new_buffer == nullptr))
				throw std::runtime_error("Unable to allocate room for synth content buffer");
			data.copy_to(new_buffer, data.size());
			vsb->s_buf = new_buffer;
			vsb->s_size = data.size()+1;
			vsb->s_len  = data.size();
			vsb->s_flags = VSB_DYNAMIC;
		}
		machine.stop();
#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent in synth syscall: %ld ns\n", nanodiff(t0, t1));
#endif
		return;
	} else if (ctx->method == VCL_MET_RECV) {
		auto& script = get_script(machine);
		// Pause the program until VCL_MET_SYNTH.
		script.set_result("synth", 100, true);
		// This will cause a retrigger of this system call once
		// we are in VCL_MET_SYNTH.
		machine.cpu.increment_pc(-4);
		machine.stop();
		return;
	}
	throw std::runtime_error(
		"Synth can only be used in vcl_synth or vcl_backend_error");
}
APICALL(cacheable)
{
	const auto* ctx = get_ctx(machine);
	if (ctx->method == VCL_MET_BACKEND_RESPONSE)
	{
		auto [set, val] = machine.sysargs<int, int> ();
		if (set) {
			riscv_SetCacheable(ctx, val);
		}
		machine.set_result(!riscv_GetCacheable(ctx));
		return;
	}
	machine.set_result(0);
}
APICALL(ttl)
{
	const auto* ctx = get_ctx(machine);
	if (ctx->method == VCL_MET_BACKEND_RESPONSE)
	{
		auto [set, val] = machine.sysargs<int, float> ();
		if (set) {
			riscv_SetTTL(ctx, val);
		}
		machine.set_result(riscv_GetTTL(ctx));
		return;
	}
	machine.set_result(0.0f);
}

APICALL(foreach_header_field)
{
	auto [where, func, data] = machine.sysargs<int, gaddr_t, gaddr_t> ();

	const auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	auto& script = get_script(machine);

	const gaddr_t first = script.guest_alloc(hp->field_count * sizeof(guest_header_field));
	gaddr_t iterator = first;
	int acount = 0;

	/* Iterate each header field */
	for (unsigned idx = HDR_FIRST; idx < hp->field_count; idx++) {
		const auto& field = hp->field_array[idx];
		const uint32_t len = field_length(field);
		/* Ignore empty fields (?) */
		if (len == 0)
			continue;

		const guest_header_field gf {(gethdr_e) where, idx, false, true};
		machine.copy_to_guest(iterator, &gf, sizeof(gf));
		iterator += sizeof(gf);
		acount ++; /* Actual */
	}

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
				http_unsetat(hp, idx - dcount++);
			}
		}
	}

	script.guest_free(first);
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
	auto* vre = get_script(machine).regex().get(index);

	auto* ctx = get_ctx(machine);
	auto* hp = get_http(ctx, (gethdr_e) where);

	/* Unset header fields from the top down */
	size_t mcount = 0;
	for (int i = hp->field_count-1; i >= HDR_FIRST; i--)
	{
		auto& field = hp->field_array[i];
		if ( VRE_exec(vre, field.begin, field.end - field.begin,
			0, 0, nullptr, 0, nullptr) >= 0 ) {
			http_unsetat(hp, i);
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
	const auto [where, index, buffer, buflen]
		= machine.sysargs<int, uint32_t, gaddr_t, uint32_t> ();

	const auto* hp = get_http(get_ctx(machine), (gethdr_e) where);

	if (is_valid_index(hp, index))
	{
		const auto& field = hp->field_array[index];
		const size_t len = field_length(field);
		if (buffer == 0 && buflen == 0) {
			machine.set_result(len);
			return;
		} else if (len > buflen) {
			machine.set_result(-1);
			return;
		}
		machine.copy_to_guest(buffer, field.begin, len);
		machine.set_result(len);
		return;
	}
	/* Null pointers */
	machine.set_result(-1);
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
	hp->field_array[idx].begin = val;
	hp->field_array[idx].end   = val + len;
	hp->field_flags[idx] = 0;
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
			hp->field_array[index].begin = src_field.begin;
			hp->field_array[index].end   = src_field.end;
			hp->field_flags[index] = 0;

			machine.set_result(field_length(src_field));
			return;
		}
		else /* In VCL you can unset a header field by assigning it
			to a non-existing other header field. */
		{
			http_unsetat(hp, index);
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
		http_unsetat(hp, index);
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

	const uint32_t hash = riscv::crc32(pattern.c_str(), pattern.size());
	const int idx = get_script(machine).regex().find(hash);
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
		get_script(machine).regex().manage(re, hash));
}
APICALL(regex_match)
{
	auto [index, buffer] = machine.sysargs<uint32_t, riscv::Buffer> ();
	auto* vre = get_script(machine).regex().get(index);
	/* VRE_exec(const vre_t *code, const char *subject, int length,
		int startoffset, int options, int *ovector, int ovecsize,
		const volatile struct vre_limits *lim) */
	if (buffer.is_sequential()) {
		machine.set_result(
			VRE_exec(vre, buffer.c_str(), buffer.size(), 0,
			0, nullptr, 0, nullptr) >= 0);
		return;
	}
	auto subject = buffer.to_string();
	machine.set_result(
		VRE_exec(vre, subject.c_str(), subject.size(), 0,
		0, nullptr, 0, nullptr) >= 0);
}
APICALL(regex_subst)
{
	auto [index, tbuffer, sbuffer, dst, maxlen]
		= machine.sysargs<uint32_t, riscv::Buffer, riscv::Buffer, gaddr_t, uint32_t> ();
	auto& script = get_script(machine);
	auto* re = script.regex().get(index);

	/* Run the regsub using existing 're' */
	const bool all = (maxlen & 0x80000000);
	const char* result;
	if (tbuffer.is_sequential() && sbuffer.is_sequential()) {
		result =
			VRT_regsub(script.ctx(), all, tbuffer.c_str(), re, sbuffer.c_str());
	} else {
		auto subject = tbuffer.to_string();
		auto subst   = sbuffer.to_string();
		result =
			VRT_regsub(script.ctx(), all, subject.c_str(), re, subst.c_str());
	}
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
	auto* re = get_script(machine).regex().get(ridx);
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
	get_script(machine).regex().free((uint32_t) index);
}

void sha256(machine_t&);

void Script::setup_syscall_interface()
{
	#define FPTR(x) machine_t::syscall_t { x }
	static constexpr std::array<const machine_t::syscall_t, ECALL_LAST - SYSCALL_BASE> handlers {
		FPTR(fail),
		FPTR(assertion_failed),
		FPTR(rvs::print),
		FPTR(shm_log),
		FPTR(breakpoint),
		FPTR(signal),
		FPTR(fail),
		FPTR(remote_call),
		FPTR(remote_strcall),

		FPTR(register_callback),
		FPTR(set_decision),
		FPTR(set_backend),
		FPTR(backend_decision),
		FPTR(ban),
		FPTR(hash_data),
		FPTR(purge),
		FPTR(synth),
		FPTR(cacheable),
		FPTR(ttl),

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
		FPTR(http_find_name),

		FPTR(regex_compile),
		FPTR(regex_match),
		FPTR(regex_subst),
		FPTR(regex_subst_hdr),
		FPTR(regex_delete),

		FPTR(sha256)
	};
	std::memcpy(&machine_t::syscall_handlers[SYSCALL_BASE],
		&handlers[0], sizeof(handlers));
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

} // rvs
