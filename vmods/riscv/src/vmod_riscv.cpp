#include "sandbox_tenant.hpp"
#include "varnish.hpp"
extern "C" {
#include "update_result.h"
}

namespace rvs {
	extern SandboxTenant* create_temporary_tenant(const SandboxTenant*, const std::string&);
	extern void delete_temporary_tenant(const SandboxTenant*);

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

} // rvs

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
const rvs::SandboxTenant* riscv_current_machine(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script) {
		return script->vrm();
	}
	return nullptr;
}

extern "C"
long riscv_current_call_idx(VRT_CTX, vcall_info info, const char* argument)
{
	using namespace rvs;

	auto* script = get_machine(ctx);
	if (script) {
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
			printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return ret;
		}
		VRT_fail(ctx, "VM call failed (invalid index given: %d)", info.idx);
		return -1;
	}
	VRT_fail(ctx, "current_call_idx() failed (no running machine)");
	return -1;
}
extern "C"
long riscv_current_resume(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script) {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		long ret = script->resume(script->max_instructions());
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in resume(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return ret;
	}
	return -1;
}

extern "C"
const char* riscv_current_name(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script)
		return script->name().c_str();
	return nullptr;
}
extern "C"
const char* riscv_current_group(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script)
		return script->group().c_str();
	return nullptr;
}
extern "C"
const char* riscv_current_result(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script)
		return script->want_result();
	return nullptr;
}
extern "C"
long riscv_current_result_value(VRT_CTX, size_t idx)
{
	auto* script = rvs::get_machine(ctx);
	if (script && idx < rvs::Script::RESULTS_MAX)
		return script->want_values().at(idx);
	return 503;
}
extern "C"
const char* riscv_current_result_string(VRT_CTX, size_t idx)
{
	auto* script = rvs::get_machine(ctx);
	if (script && idx < rvs::Script::RESULTS_MAX)
		return script->want_workspace_string(idx);
	return nullptr;
}
extern "C"
int  riscv_current_is_paused(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script)
		return script->is_paused();
	return 0;
}
extern "C"
int  riscv_current_apply_hash(VRT_CTX)
{
	auto* script = rvs::get_machine(ctx);
	if (script)
		return script->apply_hash();
	return 0;
}
