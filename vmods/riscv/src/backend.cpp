#include "sandbox_tenant.hpp"
#include "varnish.hpp"

extern "C" {
#  include "riscv_backend.h"
}

namespace rvs {

inline Script* get_machine(VRT_CTX, const void* key)
{
	auto* priv_task = VRT_priv_task(ctx, key);
	//printf("priv_task: ctx=%p bo=%p key=%p task=%p\n", ctx, ctx->bo, key, priv_task);
	if (priv_task->priv && priv_task->len == SCRIPT_MAGIC)
		return (Script*) priv_task->priv;
	return nullptr;
}

inline const char* optional_copy(VRT_CTX, const riscv::Buffer& buffer)
{
	if (buffer.is_sequential())
		return buffer.data();

	char* data = (char*) WS_Alloc(ctx->ws, buffer.size());
	// TODO: move to heap
	if (data == nullptr)
		throw std::runtime_error("Out of workspace");
	buffer.copy_to(data, buffer.size());
	return data;
}

} // rvs

extern "C"
void riscv_backend_call(VRT_CTX, const void* key, long func, long farg,
	struct backend_result *result)
{
	using namespace rvs;
	auto* script = get_machine(ctx, ctx->bo);
	(void) key;
	if (script) {
		auto* old_ctx = script->ctx();
		try {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t1);
		#endif
			/* Use backend ctx which can write to beresp */
			script->set_ctx(ctx);
			/* Call the backend response function */
			auto& machine = script->machine();
			machine.vmcall(func, (Script::gaddr_t) farg);
			/* Restore old ctx for backend_response */
			script->set_ctx(old_ctx);

			/* Get content-type, data and status */
			const auto [status, type, data, datalen] =
				machine.sysargs<int, riscv::Buffer, Script::gaddr_t, Script::gaddr_t> ();
			/* Return content-type, status, and iovecs containing data */
			using vBuffer = riscv::vBuffer;
			result->type = optional_copy(ctx, type);
			result->tsize = type.size();
			result->status = status;
			result->content_length = datalen;
			result->bufcount = machine.memory.gather_buffers_from_range(
				result->bufcount, (vBuffer *)result->buffers, data, datalen);

		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in backend_call(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return;
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
		script->set_ctx(old_ctx);
	}
	/* An error result */
	new (result) backend_result {nullptr, 0,
		500, /* Internal server error */
		0,
		0, {}
	};
}
