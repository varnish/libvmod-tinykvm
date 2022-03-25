#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <stdexcept>
using namespace kvm;
extern "C" {
#include "vtim.h"
#include "kvm_backend.h"
}

static void memory_error_handling(VRT_CTX, const tinykvm::MemoryException& e)
{
	if (e.addr() < 0x100) { /* Null-pointer speculation */
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
	/* Allow successes, redirects and client and server errors */
	if (LIKELY(code >= 200 && code < 600))
		return code;
	[[unlikely]]
	throw tinykvm::MachineException("Invalid status code returned by VM program", code);
}

static inline void kvm_ts(struct vsl_log *vsl, const char *event,
		double work, double& prev, double now)
{
	VSLb_ts(vsl, event, work, &prev, now);
}

extern "C"
void kvm_backend_call(VRT_CTX, kvm::VMPoolItem* slot,
	const char *farg, struct backend_post *post, struct backend_result *result)
{
	double t_prev = VTIM_real();
	double t_work = VTIM_real();

	auto& machine = *slot->mi;
	machine.set_ctx(ctx);
	VSLb(ctx->vsl, SLT_VCL_Log, "Tenant: %s", machine.name().c_str());
	kvm_ts(ctx->vsl, "ProgramStart", t_work, t_prev, VTIM_real());
	try {
		const auto& prog = machine.instance();
		const auto timeout = machine.tenant().config.max_time();
		auto& vm = machine.machine();
		auto fut = slot->tp.enqueue(
		[&] {
			if (post == nullptr) {
				/* Call the backend compute function */
				vm.timed_vmcall(prog.entry_at(ProgramEntryIndex::BACKEND_COMP),
					timeout, farg,
					(int) HDR_BEREQ, (int) HDR_BERESP);
			} else if (post->process_func == 0x0) {
				/* Call the backend POST function */
				auto vm_entry_addr = prog.entry_at(ProgramEntryIndex::BACKEND_POST);
				if (UNLIKELY(vm_entry_addr == 0x0))
					throw std::runtime_error("The POST callback has not been registered");
				vm.timed_vmcall(vm_entry_addr,
					timeout, farg,
					(uint64_t) post->address, (uint64_t) post->length);
			} else {
				/* Call the backend streaming POST function */
				vm.timed_vmcall(prog.entry_at(ProgramEntryIndex::BACKEND_STREAM),
					timeout, farg,
					(uint64_t) post->length);
			}
			kvm_ts(ctx->vsl, "ProgramResponse", t_work, t_prev, VTIM_real());

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			/* Get content-type and data */
			auto regs = vm.registers();
			const uint16_t status = regs.rdi;
			const uint64_t tvaddr = regs.rsi;
			const uint16_t tlen   = regs.rdx;
			const uint64_t cvaddr = regs.rcx;
			const uint64_t clen   = regs.r8;

			/* Immediately copy out content-type because small. */
			char *tbuf = (char *)WS_Alloc(ctx->ws, tlen);
			if (UNLIKELY(tbuf == nullptr)) {
				throw std::runtime_error("Out of workspace for backend VM content-type");
			}
			vm.copy_from_guest(tbuf, tvaddr, tlen);

			/* Return content-type, content-length and buffers */
			result->type = tbuf;
			result->tsize = tlen;
			result->status = sanitize_status_code(status);
			result->content_length = clen;
			result->bufcount = vm.gather_buffers_from_range(
				result->bufcount, (tinykvm::Machine::Buffer *)result->buffers, cvaddr, clen);
			kvm_ts(ctx->vsl, "ProgramProcess", t_work, t_prev, VTIM_real());
		});
		kvm_ts(ctx->vsl, "ProgramQueue", t_work, t_prev, VTIM_real());
		fut.get();
		return;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		fprintf(stderr, "%s: Backend VM timed out (%f seconds)\n",
			machine.name().c_str(), mte.seconds());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "%s: Backend VM exception: %s (data: 0x%lX)\n",
			machine.name().c_str(), e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"%s: Backend VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(ctx, e);
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
	}
	/* Make sure no SMP work is in-flight. */
	machine.machine().smp_wait();
	/* An error result */
	new (result) backend_result {nullptr, 0,
		500, /* Internal server error */
		0,
		0, {}
	};
}

extern "C"
int kvm_backend_stream(struct backend_post *post,
	const void* data_ptr, ssize_t data_len, int last)
{
	assert(post && post->slot);
	auto& slot = *(kvm::VMPoolItem *)post->slot;
	auto& mi = *slot.mi;
	try {
		auto& vm = mi.machine();

		const uint64_t GADDR = 0x40000;

		/* Copy the data segment into VM */
		vm.copy_to_guest(GADDR, data_ptr, data_len);

		/* Call the backend streaming function */
		const auto timeout = mi.tenant().config.max_time();
		vm.timed_vmcall(mi.instance().entry_at(ProgramEntryIndex::BACKEND_STREAM),
			timeout, (uint64_t) GADDR, (uint64_t) data_len,
			(uint64_t) post->length, !!last);

		/* Get content-type and data */
		auto regs = vm.registers();
		return (regs.rdi == (uint64_t)data_len) ? 0 : -1;

	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "Backend VM exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
		VSLb(post->ctx->vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const tinykvm::MemoryException& e) {
		memory_error_handling(post->ctx, e);
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
		VSLb(post->ctx->vsl, SLT_Error,
			"VM call exception: %s", e.what());
	}
	/* An error result */
	return -1;
}
