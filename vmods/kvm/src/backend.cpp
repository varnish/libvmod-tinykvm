#include "tenant_instance.hpp"
#include "varnish.hpp"
using namespace kvm;

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
static inline backend_buffer backend_error() {
	return backend_buffer {nullptr, 0, nullptr, 0};
}
/*inline const char* optional_copy(VRT_CTX, const tinykvm::Buffer& buffer)
{
	char* data = (char*) WS_Alloc(ctx->ws, buffer.size());
	// TODO: move to heap
	if (data == nullptr)
		throw std::runtime_error("Out of workspace");
	buffer.copy_to(data, buffer.size());
	return data;
}*/

extern "C"
struct backend_buffer kvm_backend_call(VRT_CTX, kvm::MachineInstance* machine,
	long func, const char *farg)
{
	auto* old_ctx = machine->ctx();
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		/* Use backend ctx which can write to beresp */
		machine->set_ctx(ctx);
		/* Call the backend response function */
		machine->machine().vmcall(func, farg,
			(int) HDR_BEREQ, (int) HDR_BERESP);
		/* Restore old ctx for backend_response */
		machine->set_ctx(old_ctx);

		/* Get content-type and data */
		auto& vm = machine->machine();
		auto regs = vm.registers();
		const uint64_t tlen = regs.rsi;
		const uint64_t clen = regs.rcx;

		char *tbuf = (char *)WS_Alloc(ctx->ws, tlen);
		char *cbuf = (char *)WS_Alloc(ctx->ws, clen);
		if (tbuf == nullptr || cbuf == nullptr) {
			throw std::runtime_error("Out of workspace for backend call result");
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
	machine->set_ctx(old_ctx);
	return backend_error();
}
