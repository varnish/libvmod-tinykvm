#include "sandbox.hpp"
#include "varnish.hpp"

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
inline backend_buffer backend_error() {
	return backend_buffer {nullptr, 0, nullptr, 0};
}
inline const char* optional_copy(VRT_CTX, const riscv::Buffer& buffer)
{
	char* data = (char*) WS_Alloc(ctx->ws, buffer.size());
	// TODO: move to heap
	if (data == nullptr)
		throw std::runtime_error("Out of workspace");
	buffer.copy_to(data, buffer.size());
	return data;
}

extern "C"
struct backend_buffer riscv_backend_call(VRT_CTX, const void* key, long func, long farg)
{
	auto* program = get_machine(ctx, key);
	if (program) {
		auto* old_ctx = program->ctx();
		try {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t1);
		#endif
			/* Use backend ctx which can write to beresp */
			program->set_ctx(ctx);
			/* Call the backend response function */
			program->machine().vmcall(func,
				(uint64_t) farg, (int) HDR_BEREQ, (int) HDR_BERESP);
			/* Restore old ctx for backend_response */
			program->set_ctx(old_ctx);

			/* Get content-type and data */
			const auto [type, data] = program->machine().sysargs<riscv::Buffer, riscv::Buffer> ();
			/* Return content-type, data, size */
			const backend_buffer result {
				.type = optional_copy(ctx, type),
				.tsize = type.size(),
				.data = optional_copy(ctx, data),
				.size = data.size()
			};
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in backend_call(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return result;
		} catch (const riscv::MachineException& e) {
			fprintf(stderr, "Backend VM exception: %s (data: 0x%lX)\n",
				e.what(), e.data());
			VSLb(ctx->vsl, SLT_Error,
				"Backend VM exception: %s (data: 0x%lX)\n",
				e.what(), e.data());
		} catch (const std::exception& e) {
			fprintf(stderr, "Backend VM exception: %s\n", e.what());
			VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		}
		program->set_ctx(old_ctx);
	}
	return backend_error();
}
