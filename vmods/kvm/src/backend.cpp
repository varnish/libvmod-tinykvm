#include "tenant_instance.hpp"
#include "machine_instance.hpp"
#include "varnish.hpp"
#include <stdexcept>
using namespace kvm;
extern "C" {
#include "kvm_backend.h"
}

extern "C"
void kvm_backend_call(VRT_CTX, kvm::TenantInstance* tenant,
	const char *func, const char *farg,
	struct backend_post *post,
	struct backend_result *result)
{
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		auto* machine = tenant->vmfork(ctx, false);
		if (UNLIKELY(machine == nullptr)) {
			throw std::runtime_error("Unable to fork backend VM");
		}

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		auto& vm = machine->machine();
		if (post == nullptr) {
			/* Call the backend response function */
			vm.vmcall(func, farg,
				(int) HDR_BEREQ, (int) HDR_BERESP);
		} else {
			/* Call the backend POST function */
			vm.vmcall(func, farg,
				(uint64_t) post->address, (uint64_t) post->length);
		}

		/* Get content-type and data */
		auto regs = vm.registers();
		const uint64_t tlen = regs.rsi;
		const uint64_t clen = regs.rcx;

		char *tbuf = (char *)WS_Alloc(ctx->ws, tlen);
		if (UNLIKELY(tbuf == nullptr)) {
			throw std::runtime_error("Out of workspace for backend VM content-type");
		}
		vm.copy_from_guest(tbuf, regs.rdi, tlen);

		/* Return content-type, content-length and buffers */
		result->type = tbuf;
		result->tsize = tlen;
		result->content_length = clen;
		result->bufcount = vm.gather_buffers_from_range(
			result->bufcount, (tinykvm::Machine::Buffer *)result->buffers, regs.rdx, clen);

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in backend_call(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return;

	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "Backend VM exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"Backend VM exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
	} catch (const std::exception& e) {
		fprintf(stderr, "Backend VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
	}
	/* An error result */
	new (result) backend_result {nullptr, 0,
		0,
		0, {}
	};
}
