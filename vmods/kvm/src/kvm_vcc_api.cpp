#include "common_defs.hpp"
#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
using namespace kvm;

extern "C"
uint64_t kvm_resolve_name(kvm::TenantInstance* tenant, const char* func)
{
	/* The tenant structure has lookup caching */
	return tenant->lookup(func);
}

extern "C"
int kvm_tenant_gucci(kvm::TenantInstance* tenant, int debug)
{
	assert(tenant);
	/* This works because the program can only be replaced with
	   another working program. It will not become null again. */
	if (!debug)
		return tenant->program != nullptr;
	else
		return tenant->debug_program != nullptr;
}

extern "C"
kvm::VMPoolItem* kvm_reserve_machine(const vrt_ctx *ctx, kvm::TenantInstance* tenant, bool debug)
{
	return tenant->vmreserve(ctx, debug);
}

extern "C"
int kvm_copy_to_machine(kvm::VMPoolItem* slot,
	uint64_t dst, const void* src, size_t len)
{
	try {
		slot->mi->copy_to(dst, src, len);
		return 0;
	} catch (...) {
		return -1;
	}
}

struct vmod_kvm_synth
{
	struct vsb *vsb;
	uint16_t status;
	char ct_buf[242];
	int  ct_len;
};

extern "C"
int kvm_synth(VRT_CTX, kvm::MachineInstance* machine,
	struct vmod_kvm_synth* synth)
{
	assert(machine && synth);

	// NOTE: CTX can change between recv and synth due to waitlist
	machine->set_ctx(ctx);
	auto& vm = machine->machine();
	auto regs = vm.registers();
	auto* vsb = synth->vsb;

	const uint16_t status = regs.rdi;
	const uint64_t tvaddr = regs.rsi;
	const uint16_t tlen   = regs.rdx;
	const uint64_t cvaddr = regs.rcx;
	const uint64_t clen   = regs.r8;

	try {
		/* Status code */
		synth->status = status;
		/* Content-Type */
		if (tlen >= sizeof(synth->ct_buf)) {
			return (-1);
		}
		synth->ct_len = tlen;
		vm.copy_from_guest(synth->ct_buf, tvaddr, tlen);
		synth->ct_buf[tlen] = 0;

		/* Gather buffers for (potentially) zero-copy content */
		tinykvm::Machine::Buffer buffers[32];
		size_t bufcount = sizeof(buffers) / sizeof(buffers[0]);

		bufcount = vm.gather_buffers_from_range(
			bufcount, buffers, cvaddr, clen);

		if (bufcount == 1) {
			/* we need to get rid of the old data */
			if (vsb->s_flags & VSB_DYNAMIC) {
				free(vsb->s_buf);
			}
			vsb->s_buf = (char*) buffers[0].ptr;
			vsb->s_size = buffers[0].len + 1; /* pretend-zero */
			vsb->s_len  = buffers[0].len;
			vsb->s_flags = VSB_FIXEDLEN;
		} else {
			VSB_clear(vsb);
			for (size_t i = 0; i < bufcount; i++) {
				VSB_bcat(vsb, buffers[i].ptr, buffers[i].len);
			}
		}
		return clen;
	} catch (const std::exception& e) {
		VRT_fail(ctx, "Invalid synth response: %s", e.what());
		return -1;
	}
}

static int kvm_forkcall(VRT_CTX, kvm::VMPoolItem* slot,
	const uint64_t addr, const char *farg)
{
	auto& machine = *slot->mi;
	machine.set_ctx(ctx);
	try {
		auto& vm = machine.machine();
		const auto timeout = machine.tenant().config.max_time();
		/* Call the backend response function */
		vm.timed_vmcall(addr, timeout, farg);
		/* Make sure no SMP work is in-flight. */
		vm.smp_wait();

		auto regs = vm.registers();
		return regs.rdi;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		fprintf(stderr, "%s: KVM VM timed out (%f seconds)\n",
			machine.name().c_str(), mte.seconds());
		VSLb(ctx->vsl, SLT_Error,
			"%s: KVM VM timed out (%f seconds)",
			machine.name().c_str(), mte.seconds());
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "%s: KVM VM exception: %s (data: 0x%lX)\n",
			machine.name().c_str(), e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"%s: KVM VM exception: %s (data: 0x%lX)",
			machine.name().c_str(), e.what(), e.data());
	} catch (const std::exception& e) {
		fprintf(stderr, "KVM VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
	}
	/* Make sure no SMP work is in-flight. */
	machine.machine().smp_wait();
	return -1;
}

extern "C"
int kvm_call(VRT_CTX, kvm::VMPoolItem* slot,
	const char *func, const char *farg)
{
	assert(func && farg);
	const auto addr = slot->mi->tenant().lookup(func);
	return kvm_forkcall(ctx, slot, addr, farg);
}

extern "C"
int kvm_callv(VRT_CTX, kvm::VMPoolItem* slot,
	const int index, const char *farg)
{
	uint64_t addr = 0x0;
	try {
		addr = slot->mi->instance().entry_at(index);
	} catch (const std::exception& e) {
		fprintf(stderr, "KVM VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		return -1;
	}
	return kvm_forkcall(ctx, slot, addr, farg);
}
