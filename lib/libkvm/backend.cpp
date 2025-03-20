/**
 * @file backend.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Glue between C and C++ for KVM backends.
 * @version 0.1
 * @date 2022-10-10
 * 
 * The main functions for handling backend GET, POST and streaming
 * POST methods. All errors thrown inside the library are ultimately
 * handled here.
 * 
 * In these functions we have already reserved a VM, and so we can now
 * communicate with it for the purposes of calling into a guests program.
 * Communication happens by enqueueing work to a single thread where the
 * VM already sits dormant. While very little work *has* to happen in
 * that thread, the actual calls into the VM has to be done inside it.
 * This is a hard requirement by KVM: Any ioctl() accessing the vCPU of
 * a VM has to be done in the thread it was created in.
 * Enqueued work can be finalized by calling .get() on the future the
 * enqueue() operation returns, which is a blocking call. If an
 * exception happens in the VM thread, it will then propagate down and
 * you can catch it.
 * 
 * We check if the program has a certain callback set, then we set up
 * the call, perform it, extract the repsonse data back, and we return
 * back to the Varnish backend code.
 * If an error happens, there is a callback that allows handling it by
 * providing a custom response instead of delivering a 503. This
 * callback has a low timeout and intended for things like error pages
 * or delivering alternative content.
 * 
 * There is a bunch of time measuring code intended to provide tenants
 * with detailed information about the run-time costs of their programs.
 * These timestamps are written to VSL and can be viewed in the
 * Sledgehammer agent program.
 * 
**/
#include "tenant_instance.hpp"
#include "paused_vm_state.hpp"
#include "program_instance.hpp"
#include "scoped_duration.hpp"
#include "settings.hpp"
#include "varnish.hpp"
#include <stdexcept>
using namespace kvm;
extern "C" {
#include "kvm_backend.h"
void kvm_varnishstat_program_exception();
void kvm_varnishstat_program_timeout();
void kvm_varnishstat_program_status(uint16_t status);
}
static constexpr bool VERBOSE_BACKEND = false;
struct backend_inputs {
	uint64_t method;
	uint64_t url;
	uint64_t arg;
	uint64_t ctype;
	uint16_t method_len;
	uint16_t url_len;
	uint16_t arg_len;
	uint16_t ctype_len;
	uint64_t data; /* Content: Can be NULL. */
	uint64_t data_len;
};

static void memory_error_handling(struct vsl_log *vsl, const tinykvm::MemoryException& e)
{
	if (e.addr() > 0x500) { /* Null-pointer speculation */
		VSLb(vsl, SLT_Error,
			"Backend VM memory exception: %s (addr: 0x%lX, size: 0x%lX)",
			e.what(), e.addr(), e.size());
	} else {
		VSLb(vsl, SLT_Error,
			"Backend VM memory exception: Null-pointer access (%s, addr: 0x%lX, size: 0x%lX)",
			e.what(), e.addr(), e.size());
	}
}

static int16_t sanitize_status_code(int16_t code)
{
	/* Allow successes, redirects, client and server errors */
	if (LIKELY(code >= 200 && code < 600))
		return code;
	throw tinykvm::MachineException("Invalid HTTP status code returned by program", code);
}

static void fetch_result(kvm::VMPoolItem* slot,
	MachineInstance& mi, struct backend_result *result)
{
	const bool regular_response = mi.response_called(1);
	const bool streaming_response = mi.response_called(10);
	if (UNLIKELY(!regular_response && !streaming_response)) {
		throw std::runtime_error("HTTP response not set. Program crashed? Check logs!");
	}

	/* Get content-type and data */
	const auto& regs = mi.machine().registers();
	const uint16_t status = regs.rdi;
	const uint64_t tvaddr = regs.rsi;
	const uint16_t tlen   = regs.rdx;

	/* Status code statistics */
	if (LIKELY(status >= 200 && status < 300)) {
		mi.stats().status_2xx++;
	} else if (UNLIKELY(status < 200)) {
		mi.stats().status_unknown ++;
	} else if (status < 400) {
		mi.stats().status_3xx++;
	} else if (status < 500) {
		mi.stats().status_4xx++;
	} else if (status < 600) {
		mi.stats().status_5xx++;
	} else {
		mi.stats().status_unknown++;
	}

	/* Immediately copy out content-type because small. */
	if (UNLIKELY(tlen >= 0x1000)) {
		throw std::runtime_error("Content-type length overflow");
	}
	char *tbuf = (char *)WS_Alloc(mi.ctx()->ws, tlen+1);
	if (UNLIKELY(tbuf == nullptr)) {
		throw std::runtime_error("Out of workspace for backend VM content-type");
	}
	mi.machine().copy_from_guest(tbuf, tvaddr, tlen);
	tbuf[tlen] = 0; /* Explicitly make content-type zero-terminated. */

	if (LIKELY(regular_response))
	{
		const uint64_t cvaddr = regs.rcx;
		const uint64_t clen   = regs.r8;

		/* Return content-type, content-length and buffers */
		result->type = tbuf;
		result->tsize = tlen;
		result->status = sanitize_status_code(status);
		result->content_length = clen;
		result->bufcount = mi.machine().gather_buffers_from_range(
			result->bufcount, (tinykvm::Machine::Buffer *)result->buffers, cvaddr, clen);
	}
	else { /* Streaming response */
		const uint64_t clen      = regs.rcx;
		const uint64_t callb_va  = regs.r8;
		const uint64_t callb_arg = regs.r9;

		if (UNLIKELY(clen == 0)) {
			throw std::runtime_error("Cannot stream zero-length response");
		}
		if (UNLIKELY(callb_va == 0x0)) {
			throw std::runtime_error("Cannot stream using invalid callback (address is 0x0)");
		}

		/* Return content-type, content-length and streaming info */
		result->type = tbuf;
		result->tsize = tlen;
		result->status = sanitize_status_code(status);
		result->content_length = clen;
		result->bufcount = 0;
		result->stream_slot = (vmod_kvm_slot *)slot;
		result->stream_vsl  = nullptr;
		result->stream_callback = callb_va;
		result->stream_argument = callb_arg;
	}
	mi.stats().output_bytes += result->content_length;
	/* Record program status counter */
	kvm_varnishstat_program_status(result->status);
}

/* Error handling is an optional callback into the request VM when an
   exception just happened during normal GET/POST. It gives tenant a
   chance to produce a proper error page, or deliver alternate content. */
static void error_handling(kvm::VMPoolItem* slot,
	const struct kvm_chain_item *invoc, struct backend_result *result, const char *exception)
{
	/* Record exception in varnish stat counter. */
	kvm_varnishstat_program_exception();

	auto& machine = *slot->mi;
	machine.stats().exceptions++;
	/* The machine should be reset (after request). */
	machine.reset_needed_now();
	/* Print sane backtrace (faulting RIP) */
	machine.print_backtrace();

	const auto& prog = machine.program();
	auto* ctx = machine.ctx();
	if (prog.entry_at(ProgramEntryIndex::BACKEND_ERROR) != 0x0) {
	try {
		auto& vm = machine.machine();
		auto fut = slot->tp.enqueue(
		[&] () -> long {
			auto& machine = *slot->mi;
			auto* ctx = machine.ctx();

			/* Enforce that guest program calls the backend_response system call. */
			machine.begin_call();
			/* Exception CPU-time. */
			ScopedDuration error_counter(machine.stats().error_cpu_time);

			/* Call the on_error callback with exception reason. */
			auto on_error_addr = prog.entry_at(ProgramEntryIndex::BACKEND_ERROR);
			if (UNLIKELY(on_error_addr == 0x0))
				throw std::runtime_error("The ERROR callback has not been registered");

			VSLb(ctx->vsl, SLT_VCL_Log,
				"%s: Calling on_error() at 0x%lX",
				machine.name().c_str(), on_error_addr);

			vm.timed_vmcall(on_error_addr,
				ERROR_HANDLING_TIMEOUT, invoc->inputs.url, invoc->inputs.argument, exception);

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			fetch_result(slot, machine, result);
			return 0L;
		});
		fut.get();
		return;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		kvm_varnishstat_program_timeout();
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(ctx->vsl, e);
	} catch (const tinykvm::MachineException& e) {
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
	} catch (const std::exception& e) {
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
	}
	}
	try {
		/* Make sure no SMP work is in-flight. */
		machine.machine().smp_wait();
		/* Print sane backtrace */
		machine.print_backtrace();
	} catch (...) {}
	/* An error result */
	new (result) backend_result {nullptr, 0,
		500, /* Internal server error */
		0,
		0, {}
	};
	/* Record program status counter */
	kvm_varnishstat_program_status(result->status);
}

static void fill_backend_inputs(
	MachineInstance& machine, __u64& stack,
	const struct kvm_chain_item *invoc,
	const struct backend_post *post,
	backend_inputs& inputs)
{
	auto& vm = machine.machine();
	inputs.method_len = __builtin_strlen(invoc->inputs.method);
	inputs.method     = vm.stack_push(stack, invoc->inputs.method, inputs.method_len + 1);
	inputs.url_len = __builtin_strlen(invoc->inputs.url);
	inputs.url     = vm.stack_push(stack, invoc->inputs.url, inputs.url_len + 1);
	inputs.arg_len = __builtin_strlen(invoc->inputs.argument);
	inputs.arg     = vm.stack_push(stack, invoc->inputs.argument, inputs.arg_len + 1);
	if (post != nullptr) {
		/* POST data information. */
		inputs.ctype_len = __builtin_strlen(invoc->inputs.content_type);
		inputs.ctype = vm.stack_push(stack, invoc->inputs.content_type, inputs.ctype_len + 1);
		inputs.data  = post->address;
		inputs.data_len = post->length;

		machine.stats().input_bytes += post->length;
	}
	else
	{
		/* Guarantee readable strings. */
		inputs.ctype = vm.stack_push_cstr(stack, "");
		inputs.ctype_len = 0;
		/* Buffers with known length can be NULL. */
		inputs.data  = 0;
		inputs.data_len = 0;
	}
}

extern "C"
void kvm_backend_call(VRT_CTX, kvm::VMPoolItem* slot,
	const struct kvm_chain_item *invoc,
	struct backend_post *post, struct backend_result *result)
{
	MachineInstance& machine = *slot->mi;
	/* Setting the VRT_CTX allows access to HTTP and VSL, etc. */
	machine.set_ctx(ctx);

	if constexpr (VERBOSE_BACKEND) {
		VSLb(ctx->vsl, SLT_VCL_Log, "Tenant: %s", machine.name().c_str());
	}

	try {
		auto fut = slot->tp.enqueue(
		[slot, invoc, post, result] () -> long {
			if constexpr (VERBOSE_BACKEND) {
				printf("Begin backend %s %s (arg=%s)\n", invoc->inputs.method,
					invoc->inputs.url, invoc->inputs.argument);
			}
			auto& machine = *slot->mi;

			/* Regular CPU-time. */
			ScopedDuration cputime(machine.stats().request_cpu_time);

			/* Enforce that guest program calls the backend_response system call. */
			machine.begin_call();
			machine.stats().invocations ++;

			const auto timeout = machine.max_req_time();
			const auto& prog = machine.program();
			auto& vm = machine.machine();
			if (post != nullptr) {
				/* Try to reduce POST mmap allocation */
				vm.mmap_relax(post->address, post->capacity, post->length);
			}

			auto on_method_addr = prog.entry_at(ProgramEntryIndex::BACKEND_METHOD);
			if (on_method_addr != 0x0) {
				/* Call the backend METHOD function */
				struct backend_inputs inputs {};
				__u64 stack = vm.stack_address();
				fill_backend_inputs(machine, stack, invoc, post, inputs);
				uint64_t struct_addr = vm.stack_push(stack, inputs);

				VSLb(machine.ctx()->vsl, SLT_VCL_Log,
					"%s: Calling on_method() at 0x%lX (URL: %s, URL len: %u, Is-Post: %d)",
					machine.name().c_str(), on_method_addr, invoc->inputs.url, inputs.url_len, post != nullptr);

				/* Call into VM doing a full pagetable/cache flush. */
				vm.timed_vmcall_stack(on_method_addr,
					stack, timeout, (uint64_t)struct_addr);

			} else if (post == nullptr && prog.entry_at(ProgramEntryIndex::BACKEND_GET) != 0x0) {
				/* Call the backend GET function */
				auto on_get_addr = prog.entry_at(ProgramEntryIndex::BACKEND_GET);
				VSLb(machine.ctx()->vsl, SLT_VCL_Log,
					"%s: Calling on_get() at 0x%lX",
					machine.name().c_str(), on_get_addr);

				/* Call into VM doing a full pagetable/cache flush. */
				vm.timed_vmcall(on_get_addr, timeout, invoc->inputs.url, invoc->inputs.argument);
			} else if (post != nullptr && prog.entry_at(ProgramEntryIndex::BACKEND_POST) != 0x0) {
				/* Call the backend POST function */
				auto on_post_addr = prog.entry_at(ProgramEntryIndex::BACKEND_POST);
				VSLb(machine.ctx()->vsl, SLT_VCL_Log,
					"%s: Calling on_post() at 0x%lX with data at 0x%lX, len %zu",
					machine.name().c_str(), on_post_addr, post->address, size_t(post->length));
				machine.stats().input_bytes += post->length;

				vm.timed_vmcall(on_post_addr,
					timeout,
					invoc->inputs.url,
					invoc->inputs.argument,
					invoc->inputs.content_type,
					uint64_t(post->address), uint64_t(post->length));
			} else if (prog.get_paused_vm_state().has_value()) {
				/* Allocate 16KB space for struct backend_inputs */
				struct backend_inputs inputs {};
				__u64 stack = vm.mmap_allocate(16384) + 16384;
				fill_backend_inputs(machine, stack, invoc, post, inputs);
				const PausedVMState& state = std::any_cast<PausedVMState> (prog.get_paused_vm_state());
				auto& regs = vm.registers();
				regs = state.regs;
				vm.set_registers(regs);
				vm.set_fpu_registers(state.fpu);
				/* RDI is address of struct backend_inputs */
				const uint64_t g_struct_addr = regs.rdi;
				vm.copy_to_guest(g_struct_addr, &inputs, sizeof(inputs));

				VSLb(machine.ctx()->vsl, SLT_VCL_Log,
					"%s: Resuming VM at PC=0x%lX",
					machine.name().c_str(), regs.rip);

				/* Resume execution */
				vm.run_in_usermode(timeout);
			} else {
				throw std::runtime_error("No backend method set and no paused VM state");
			}

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			if constexpr (VERBOSE_BACKEND) {
				printf("Finish backend %s %s\n", invoc->inputs.method, invoc->inputs.url);
			}
			/* Verify response and fill out result struct. */
			fetch_result(slot, machine, result);
			return 0L;
		});
		fut.get();
		return;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		machine.stats().timeouts++;
		kvm_varnishstat_program_timeout();
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, invoc, result, mte.what());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(ctx->vsl, e);
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, invoc, result, e.what());
	} catch (const tinykvm::MachineException& e) {
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, invoc, result, e.what());
	} catch (const std::exception& e) {
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, invoc, result, e.what());
	}
}

extern "C"
int kvm_backend_streaming_post(struct backend_post *post,
	const void* data_ptr, ssize_t data_len)
{
	assert(post && post->slot);
	auto& slot = *(kvm::VMPoolItem *)post->slot;
	auto& mi = *slot.mi;
	try {
		auto& vm = mi.machine();

		/* Not an overflow check, just stopping wildly large payloads. */
		if (UNLIKELY(post->length + data_len > post->capacity)) {
			throw tinykvm::MachineException("POST request too large", post->capacity);
		}

		/* Call the backend streaming function, if set. */
		const auto call_addr =
			mi.program().entry_at(ProgramEntryIndex::BACKEND_STREAM);
		if (call_addr != 0x0) {
			auto fut = slot.tp.enqueue(
			[&] () -> long {
				/* Regular CPU-time. */
				ScopedDuration cputime(mi.stats().request_cpu_time);

				unsigned long long rsp = vm.stack_address();
				auto data_vaddr = vm.stack_push(rsp, data_ptr, size_t(data_len));

				const auto timeout = mi.max_req_time();
				vm.timed_vmcall_stack(call_addr, rsp, timeout,
					post->inputs.url, post->inputs.argument,
					post->inputs.content_type,
					(uint64_t)data_vaddr, (uint64_t)data_len,
					(uint64_t)post->length);
				return 0L;
			});
			fut.get();

			/* Increment POST length *after* VM call. */
			post->length += data_len;

			/* Verify that the VM consumed all the bytes. */
			return ((ssize_t)vm.return_value() == data_len) ? 0 : -1;
		}
		else
		{
			/* Regular POST, first payload: Allocate payload in VM. */
			if (post->length == 0)
			{
				assert(post->address == 0);
				post->address = mi.allocate_post_data(post->capacity);
			}

			/* Copy the data segment into VM at the right offset,
			   building a sequential, complete buffer. */
			vm.copy_to_guest(post->address + post->length, data_ptr, data_len);
		}

		/* Increment POST length, no VM call. */
		post->length += data_len;
		return 0;

	} catch (const tinykvm::MemoryException& e) {
		mi.stats().exceptions++;
		memory_error_handling(post->ctx->vsl, e);
	} catch (const tinykvm::MachineTimeoutException& e) {
		mi.stats().timeouts++;
		VSLb(post->ctx->vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const tinykvm::MachineException& e) {
		mi.stats().exceptions++;
		VSLb(post->ctx->vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const std::exception& e) {
		mi.stats().exceptions++;
		VSLb(post->ctx->vsl, SLT_Error,
			"VM call exception: %s", e.what());
	}
	/* Record exception in varnish stat counter. */
	kvm_varnishstat_program_exception();
	/* An error result */
	return -1;
}

extern "C"
ssize_t kvm_backend_streaming_delivery(
	struct backend_result *result, void* dst,
	ssize_t max_len, ssize_t written)
{
	assert(result && result->stream_callback);
	auto& slot = *(kvm::VMPoolItem *)result->stream_slot;
	auto& mi = *slot.mi;
	mi.set_ctx(nullptr);
	try {
		auto& vm = mi.machine();

		/* Call the backend streaming function, if set. */
		auto fut = slot.tp.enqueue(
		[&] () -> long {
			/* Regular CPU-time. */
			ScopedDuration cputime(mi.stats().request_cpu_time);

			const auto timeout = STREAM_HANDLING_TIMEOUT;
			vm.timed_vmcall(result->stream_callback, timeout,
				(uint64_t)result->stream_argument,
				(uint64_t)max_len, (uint64_t)written,
				(uint64_t)result->content_length);
			return 0L;
		});
		fut.get();

		const auto& regs = vm.registers();
		/* NOTE: RAX gets moved to RDI. RDX is length.
		   NOTE: We use unsigned min to avoid negative values! */
		size_t len = std::min((size_t)max_len, (size_t)regs.rdx);
		vm.copy_from_guest(dst, regs.rdi, len);

		return len;

	} catch (const tinykvm::MemoryException& e) {
		mi.stats().exceptions++;
		memory_error_handling(result->stream_vsl, e);
	} catch (const tinykvm::MachineTimeoutException& e) {
		mi.stats().timeouts++;
		VSLb(result->stream_vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const tinykvm::MachineException& e) {
		mi.stats().exceptions++;
		VSLb(result->stream_vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const std::exception& e) {
		mi.stats().exceptions++;
		VSLb(result->stream_vsl, SLT_Error,
			"VM call exception: %s", e.what());
	}
	/* Record exception in varnish stat counter. */
	kvm_varnishstat_program_exception();
	/* An error result */
	return -1;
}
