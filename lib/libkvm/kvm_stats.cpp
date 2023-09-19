/**
 * @file kvm_stats.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief VMOD KVM helper functions for VCL calls.
 * @version 0.1
 * @date 2022-09-18
 * 
 * Statistics around programs.
 * 
 */
#include "common_defs.hpp"
#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <atomic>
#include <nlohmann/json.hpp>
using namespace kvm;

using foreach_function_t = int(*)(const char *, kvm::TenantInstance*, void *);
extern "C" int kvm_tenant_foreach(VCL_PRIV task, foreach_function_t func, void* state);

namespace kvm {
static auto gather_stats(const MachineInstance& mi)
{
	const auto& stats = mi.stats();

	return nlohmann::json::object({
		{"invocations", stats.invocations},
		{"resets",      stats.resets},
		{"exceptions",  stats.exceptions},
		{"timeouts",    stats.timeouts},
		{"request_cpu_time",   stats.request_cpu_time},
		{"exception_cpu_time", stats.error_cpu_time},
		{"input_bytes", stats.input_bytes},
		{"output_bytes",stats.output_bytes},
		{"status_2xx",  stats.status_2xx},
		{"status_3xx",  stats.status_3xx},
		{"status_4xx",  stats.status_4xx},
		{"status_5xx",  stats.status_5xx},
		{"vm_main_memory",   mi.tenant().config.max_main_memory()},
		{"vm_bank_capacity", mi.tenant().config.max_req_memory()},
		{"vm_bank_highest",  mi.machine().banked_memory_capacity_bytes()},
		{"vm_bank_current",  mi.machine().banked_memory_bytes()},
	});
}
static void gather_stats(VRT_CTX,
	nlohmann::json& j, TenantInstance* tenant)
{
	static constexpr bool debug = false;

	std::shared_ptr<ProgramInstance> prog;
	if (LIKELY(!debug))
		prog = std::atomic_load(&tenant->program);
	else
		prog = std::atomic_load(&tenant->debug_program);

	/* Don't gather stats for missing programs. */
	if (prog == nullptr) {
		VSLb(ctx->vsl, SLT_VCL_Log,
			"compute: Did not gather stats (missing program)");
		return;
	}

	/* JSON object root uses program name. */
	auto& obj = j[tenant->config.name];

	/* Storage VM */
	if (prog->has_storage())
	{
		auto& storage = prog->storage();

		obj["storage"] = {
			gather_stats(*storage.storage_vm),
		};
	}

	MachineStats totals {};
	auto& requests = obj["request"];

	/* Individual request VMs */
	for (size_t i = 0; i < prog->m_vms.size(); i++)
	{
		auto& mi = *prog->m_vms[i].mi;
		requests["req" + std::to_string(i)] = {
			gather_stats(mi),
		};

		totals.invocations += mi.stats().invocations;
		totals.exceptions += mi.stats().exceptions;
		totals.resets += mi.stats().resets;
		totals.timeouts += mi.stats().timeouts;

		totals.request_cpu_time += mi.stats().request_cpu_time;
		totals.error_cpu_time += mi.stats().error_cpu_time;

		totals.input_bytes += mi.stats().input_bytes;
		totals.output_bytes += mi.stats().output_bytes;

		totals.status_2xx += mi.stats().status_2xx;
		totals.status_3xx += mi.stats().status_3xx;
		totals.status_4xx += mi.stats().status_4xx;
		totals.status_5xx += mi.stats().status_5xx;
		totals.status_unknown += mi.stats().status_unknown;
	}

	/* Cumulative totals */
	requests["totals"] = {
		{"invocations", totals.invocations},
		{"resets",      totals.resets},
		{"exceptions",  totals.exceptions},
		{"timeouts",    totals.timeouts},
		{"request_cpu_time",   totals.request_cpu_time},
		{"exception_cpu_time", totals.error_cpu_time},
		{"input_bytes", totals.input_bytes},
		{"output_bytes",totals.output_bytes},
		{"status_2xx",  totals.status_2xx},
		{"status_3xx",  totals.status_3xx},
		{"status_4xx",  totals.status_4xx},
		{"status_5xx",  totals.status_5xx},
	};
}
} // kvm

/* Program statistics -> JSON */
extern "C"
const char * kvm_json_stats(VRT_CTX, VCL_PRIV task, const char *pattern, unsigned indent)
{
	if (ctx->ws == NULL)
		return "{}";

	vre_t *re = nullptr;
	try {
		/* Compile regex pattern (NOTE: uses a lot of stack). */
		const char *error = "";
		int error_offset = 0;
		re = VRE_compile(pattern, 0, &error, &error_offset);

		if (re == NULL)
		{
			/* TODO: Nice regex error explanation. */
			(void)error;
			(void)error_offset;
			VRT_fail(ctx,
				 "compute: Regex '%s' compilation failed", pattern);
			return "{}";
		}

		struct {
			nlohmann::json j;
			VRT_CTX;
			vre_t *regex;
		} state;
		state.ctx =  ctx;
		state.regex = re;

		kvm_tenant_foreach(task, [] (const char *program, auto *tenant, void *vstate) -> int
		{
			auto& s = *(decltype(state)*)vstate;
#ifdef VARNISH_PLUS
			const int matches =
				VRE_exec(s.regex, program, strlen(program), 0,
					0, NULL, 0, NULL);
#else
			const int matches = 0;
#endif
			if (matches > 0) {
				VSLb(s.ctx->vsl, SLT_VCL_Log,
					"compute: Adding stats from '%s'", program);
				kvm::gather_stats(s.ctx, s.j, tenant);
				return 1;
			}
			return 0;

		}, &state);

		VRE_free(&re);

		const auto output = state.j.dump(indent);

		auto* ws_json = (char *)WS_Alloc(ctx->ws, output.size() + 1);
		if (ws_json == nullptr) {
			VRT_fail(ctx,
				"kvm: Out of workspace for statistics (L=%zu)", output.size());
			return "{}";
		}
		__builtin_memcpy(ws_json, output.data(), output.size());
		ws_json[output.size()] = 0; /* Zero-termination */

		return ws_json;

	} catch (const std::exception& e) {
		if (re)
			VRE_free(&re);
		VRT_fail(ctx,
			"kvm: Exception when creating statistics: %s", e.what());
		return "{}";
	}
}
