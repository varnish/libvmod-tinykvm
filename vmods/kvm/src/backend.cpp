#include "tenant_instance.hpp"
#include "machine_instance.hpp"
#include "varnish.hpp"
#include <stdexcept>
using namespace kvm;

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
static inline backend_buffer kvm_backend_error() {
	return backend_buffer {nullptr, 0, nullptr, 0};
}

extern "C"
struct backend_buffer kvm_backend_call(VRT_CTX, kvm::TenantInstance* tenant,
	const char *func, const char *farg)
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
		/* Call the backend response function */
		machine->machine().vmcall(func, farg,
			(int) HDR_BEREQ, (int) HDR_BERESP);

		/* Get content-type and data */
		auto& vm = machine->machine();
		auto regs = vm.registers();
		const uint64_t tlen = regs.rsi;
		const uint64_t clen = regs.rcx;

		char *tbuf = (char *)WS_Alloc(ctx->ws, tlen);
		char *cbuf = (char *)WS_Alloc(ctx->ws, clen);
		if (UNLIKELY(tbuf == nullptr || cbuf == nullptr)) {
			throw std::runtime_error("Out of workspace for backend VM result");
		}

		vm.copy_from_guest(tbuf, regs.rdi, tlen);
		vm.copy_from_guest(cbuf, regs.rdx, clen);

		/* Return content-type, data, size */
		const backend_buffer result {
			.type = tbuf,
			.tsize = tlen,
			.data = cbuf,
			.size = clen
		};

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in backend_call(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return result;

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
	return kvm_backend_error();
}
