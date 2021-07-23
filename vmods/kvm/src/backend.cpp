#include "tenant_instance.hpp"
#include "varnish.hpp"
extern MachineInstance* get_machine(VRT_CTX, const void* key);

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
inline backend_buffer backend_error() {
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
struct backend_buffer kvm_backend_call(VRT_CTX, MachineInstance* machine, long func, long farg)
{
	auto* old_ctx = machine->ctx();
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		/* Use backend ctx which can write to beresp */
		machine->set_ctx(ctx);
		/* Call the backend response function */
		machine->machine().vmcall(func,
			(uint64_t) farg, (int) HDR_BEREQ, (int) HDR_BERESP);
		/* Restore old ctx for backend_response */
		machine->set_ctx(old_ctx);

		/* Get content-type and data */
		//const auto [type, data] = machine->machine().sysargs<riscv::Buffer, riscv::Buffer> ();
		/* Return content-type, data, size */
		const backend_buffer result {
			.type = "text/html",
			.tsize = 9,
			.data = "resp",
			.size = 4
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
}
