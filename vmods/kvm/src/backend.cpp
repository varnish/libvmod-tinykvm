/**
 * @file backend.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Glue between C and C++ for KVM backends.
 * @version 0.1
 * @date 2022-07-23
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
#include "program_instance.hpp"
#include "settings.hpp"
#include "varnish.hpp"
#include <stdexcept>
using namespace kvm;
extern "C" {
#include "vtim.h"
#include "kvm_backend.h"
}
static constexpr bool VERBOSE_BACKEND = false;

static void memory_error_handling(VRT_CTX, const tinykvm::MemoryException& e)
{
	if (e.addr() < 0x500) { /* Null-pointer speculation */
	fprintf(stderr,
		"Backend VM memory exception: %s (addr: 0x%lX, size: 0x%lX)\n",
		e.what(), e.addr(), e.size());
	VSLb(ctx->vsl, SLT_Error,
		"Backend VM memory exception: %s (addr: 0x%lX, size: 0x%lX)",
		e.what(), e.addr(), e.size());
	} else {
		fprintf(stderr,
			"Backend VM memory exception: Null-pointer access\n");
		VSLb(ctx->vsl, SLT_Error,
			"Backend VM memory exception: Null-pointer access");
	}
}

static int16_t sanitize_status_code(int16_t code)
{
	/* Allow successes, redirects, client and server errors */
	if (LIKELY(code >= 200 && code < 600))
		return code;
	[[unlikely]]
	throw tinykvm::MachineException("Invalid HTTP status code returned by program", code);
}

static inline void kvm_ts(struct vsl_log *vsl, const char *event,
		double work, double& prev, double now)
{
	VSLb_ts(vsl, event, work, &prev, now);
}

static void fetch_result(MachineInstance& mi, struct backend_result *result)
{
	if (UNLIKELY(!mi.response_called(1))) {
		throw std::runtime_error("HTTP response not set. Program crashed? Check logs!");
	}

	/* Get content-type and data */
	const auto& regs = mi.machine().registers();
	const uint16_t status = regs.rdi;
	const uint64_t tvaddr = regs.rsi;
	const uint16_t tlen   = regs.rdx;
	const uint64_t cvaddr = regs.rcx;
	const uint64_t clen   = regs.r8;

	/* Immediately copy out content-type because small. */
	char *tbuf = (char *)WS_Alloc(mi.ctx()->ws, tlen);
	if (UNLIKELY(tbuf == nullptr)) {
		throw std::runtime_error("Out of workspace for backend VM content-type");
	}
	mi.machine().copy_from_guest(tbuf, tvaddr, tlen);

	/* Return content-type, content-length and buffers */
	result->type = tbuf;
	result->tsize = tlen;
	result->status = sanitize_status_code(status);
	result->content_length = clen;
	result->bufcount = mi.machine().gather_buffers_from_range(
		result->bufcount, (tinykvm::Machine::Buffer *)result->buffers, cvaddr, clen);
}

/* Error handling is an optional callback into the request VM when an
   exception just happened during normal GET/POST. It gives tenant a
   chance to produce a proper error page, or deliver alternate content. */
static void error_handling(kvm::VMPoolItem* slot,
	const char *farg[2], struct backend_result *result, const char *exception)
{
	auto& machine = *slot->mi;
	/* Print sane backtrace (faulting RIP) */
	machine.print_backtrace();

	const auto& prog = machine.program();
	auto* ctx = machine.ctx();
	if (prog.entry_at(ProgramEntryIndex::ON_ERROR) != 0x0) {
	try {
		auto& vm = machine.machine();
		auto fut = slot->tp.enqueue(
		[&] {
			/* Enforce that guest program calls the backend_response system call. */
			machine.begin_call();

			/* Call the on_error callback with exception reason. */
			auto vm_entry_addr = prog.entry_at(ProgramEntryIndex::ON_ERROR);
			if (UNLIKELY(vm_entry_addr == 0x0))
				throw std::runtime_error("The GET callback has not been registered");
			vm.timed_reentry(vm_entry_addr,
				ERROR_HANDLING_TIMEOUT, farg[0], exception);

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			fetch_result(machine, result);
		});
		fut.get();
		return;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		fprintf(stderr, "%s: Backend VM timed out (%f seconds)\n",
			machine.name().c_str(), mte.seconds());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(ctx, e);
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "%s: Backend VM exception: %s (data: 0x%lX)\n",
			machine.name().c_str(), e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
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
}

extern "C"
void kvm_backend_call(VRT_CTX, kvm::VMPoolItem* slot,
	const char *farg[2], struct backend_post *post, struct backend_result *result)
{
	double t_prev = VTIM_real();
	double t_work = t_prev;

	auto& machine = *slot->mi;
	/* Setting the VRT_CTX allows access to HTTP and VSL, etc. */
	machine.set_ctx(ctx);
	VSLb(ctx->vsl, SLT_VCL_Log, "Tenant: %s", machine.name().c_str());
	kvm_ts(ctx->vsl, "ProgramStart", t_work, t_prev, VTIM_real());
	try {
		auto fut = slot->tp.enqueue(
		[&] {
			if constexpr (VERBOSE_BACKEND) {
				printf("Begin backend GET %s\n", farg[0]);
			}
			kvm_ts(ctx->vsl, "ProgramCall", t_work, t_prev, VTIM_real());
			/* Enforce that guest program calls the backend_response system call. */
			machine.begin_call();

			const auto timeout = machine.max_req_time();
			const auto& prog = machine.program();
			auto& vm = machine.machine();

			if (post == nullptr) {
				/* Call the backend compute function */
				auto vm_entry_addr = prog.entry_at(ProgramEntryIndex::BACKEND_COMP);
				if (UNLIKELY(vm_entry_addr == 0x0))
					throw std::runtime_error("The GET callback has not been registered");
				if (machine.is_ephemeral()) {
					/* Call into VM doing a full pagetable/cache flush. */
					vm.timed_vmcall(vm_entry_addr, timeout, farg[0]);
				} else {
					/* Call into VM without flushing anything. */
					vm.timed_reentry(vm_entry_addr, timeout, farg[0]);
				}
			} else {
				/* Try to reduce POST mmap allocation */
				vm.mmap_relax(post->address, post->capacity, post->length);
				/* Call the backend POST function */
				auto vm_entry_addr = prog.entry_at(ProgramEntryIndex::BACKEND_POST);
				if (UNLIKELY(vm_entry_addr == 0x0))
					throw std::runtime_error("The POST callback has not been registered");
				vm.timed_vmcall(vm_entry_addr,
					timeout, farg[0],
					(uint64_t) post->address, (uint64_t) post->length);
			}
			kvm_ts(ctx->vsl, "ProgramResponse", t_work, t_prev, VTIM_real());

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			if constexpr (VERBOSE_BACKEND) {
				printf("Finish backend GET %s\n", farg[0]);
			}
			fetch_result(machine, result);

			kvm_ts(ctx->vsl, "ProgramProcess", t_work, t_prev, VTIM_real());
		});
		/* XXX: This competes with the VSL changes in the VM thread. */
		//kvm_ts(ctx->vsl, "ProgramQueue", t_work, t_prev, VTIM_real());
		fut.get();
		return;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		fprintf(stderr, "%s: Backend VM timed out (%f seconds)\n",
			machine.name().c_str(), mte.seconds());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, farg, result, mte.what());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(ctx, e);
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, farg, result, e.what());
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "%s: Backend VM exception: %s (data: 0x%lX)\n",
			machine.name().c_str(), e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, farg, result, e.what());
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		/* Try again if on_error has been set, otherwise 500. */
		error_handling(slot, farg, result, e.what());
	}
}

extern "C"
int kvm_backend_stream(struct backend_post *post,
	const void* data_ptr, ssize_t data_len)
{
	assert(post && post->slot);
	auto& slot = *(kvm::VMPoolItem *)post->slot;
	auto& mi = *slot.mi;
	try {
		auto& vm = mi.machine();

		/* Copy the data segment into VM */
		vm.copy_to_guest(post->address, data_ptr, data_len);

		/* Call the backend streaming function, if set. */
		const auto call_addr =
			mi.program().entry_at(ProgramEntryIndex::BACKEND_STREAM);
		if (call_addr != 0x0) {
			auto fut = slot.tp.enqueue(
			[&] {
				const auto timeout = mi.max_req_time();
				if (post->length == 0) {
					vm.timed_vmcall(call_addr, timeout,
						post->argument,
						(uint64_t)post->address, (uint64_t)data_len,
						(uint64_t)post->length);
				} else {
					vm.timed_reentry(call_addr, timeout,
						post->argument,
						(uint64_t)post->address, (uint64_t)data_len,
						(uint64_t)post->length);
				}
			});
			fut.get();

			/* Increment POST length *after* VM call. */
			post->length += data_len;

			/* Verify that the VM consumed all the bytes. */
			return ((ssize_t)vm.return_value() == data_len) ? 0 : -1;
		}

		/* Increment POST length, no VM call. */
		post->length += data_len;
		return 0;

	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(post->ctx, e);
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "Backend VM exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
		VSLb(post->ctx->vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
		VSLb(post->ctx->vsl, SLT_Error,
			"VM call exception: %s", e.what());
	}
	/* An error result */
	return -1;
}
