#include "sandbox_tenant.hpp"
#include "varnish.hpp"
extern "C" {
#include "update_result.h"
}
struct CallResults {
	long results[3];
};

namespace rvs
{
	inline Script* get_machine(VRT_CTX, const void* key)
	{
		auto* priv_task = VRT_priv_task(ctx, key);
		//printf("priv_task: ctx=%p bo=%p key=%p task=%p\n", ctx, ctx->bo, key, priv_task);
		if (priv_task->priv && (unsigned)priv_task->len == SCRIPT_MAGIC)
			return (Script*) priv_task->priv;
		return nullptr;
	}
	inline Script* get_machine(VRT_CTX)
	{
		if (ctx->req)
			return get_machine(ctx, ctx->req);
		return get_machine(ctx, ctx->bo);
	}
}

extern "C"
rvs::Script* riscv_fork(VRT_CTX, const char* tenant, int debug)
{
	using namespace rvs;

	extern SandboxTenant* tenant_find(VRT_CTX, const char* name);
	auto* tenptr = tenant_find(ctx, tenant);
	if (UNLIKELY(tenptr == nullptr))
		return nullptr;

	return tenptr->vmfork(ctx, debug);
}

extern "C"
int riscv_delete(VRT_CTX)
{
	using namespace rvs;

	auto* script = rvs::get_machine(ctx);
	if (script)
	{
		script->~Script();
		return 0;
	}
	return -1;
}

extern "C"
int  riscv_apply_hash(rvs::Script* script)
{
	return script->apply_hash();
}

extern "C"
long riscv_call_idx(rvs::Script* script, VRT_CTX, vcall_info info, const char* argument)
{
	using namespace rvs;

	const auto& callbacks = script->program().callback_entries;
	if (info.idx < callbacks.size())
	{
		auto addr = callbacks[info.idx];
		if (addr == 0x0) {
			VSLb(ctx->vsl, SLT_Error,
				"VM call '%s' skipped: The function at index %d is not available",
				callback_names.at(info.idx), info.idx);
			return -1;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		// VRT ctx can easily change even on the same request due to waitlist
		script->set_ctx(ctx);
		int ret = 0;
		if (argument != nullptr)
			ret = script->call(addr, argument, (int) info.arg1, (int) info.arg2);
		else
			ret = script->call(addr, (int) info.arg1, (int) info.arg2);
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		timing_vmcall.add(t1, t2);
	#endif
		return ret;
	}
	VRT_fail(ctx, "VM call failed (invalid index given: %d)", info.idx);
	return -1;
}

extern "C"
long riscv_result_values(rvs::Script* script, struct CallResults *cr)
{
	cr->results[0] = script->want_values().at(0);
	cr->results[1] = script->want_values().at(1);
	cr->results[2] = script->want_values().at(2);
	return 0;
}
