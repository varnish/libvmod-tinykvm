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
#include "scoped_duration.hpp"
#include "serialized_state.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <atomic>
#include <nlohmann/json.hpp>
using namespace kvm;

using foreach_function_t = int(*)(const char *, kvm::TenantInstance*, void *);
extern "C" int kvm_tenant_foreach(VCL_PRIV task, foreach_function_t func, void* state);

namespace kvm {
template <typename TT>
static auto gather_stats(const MachineInstance& mi, TT& taskq)
{
	const auto& stats = mi.stats();

	return nlohmann::json::object({
		{"invocations", stats.invocations},
		{"resets",      stats.resets},
		{"resets_full", stats.full_resets},
		{"exceptions",  stats.exceptions},
		{"exception_oom", stats.exception_oom},
		{"exception_mem", stats.exception_mem},
		{"timeouts",    stats.timeouts},
		{"reservation_time",   stats.reservation_time},
		{"reset_time",         stats.vm_reset_time},
		{"request_cpu_time",   stats.request_cpu_time},
		{"exception_cpu_time", stats.error_cpu_time},
		{"input_bytes", stats.input_bytes},
		{"output_bytes",stats.output_bytes},
		{"status_2xx",  stats.status_2xx},
		{"status_3xx",  stats.status_3xx},
		{"status_4xx",  stats.status_4xx},
		{"status_5xx",  stats.status_5xx},
		{"vm_address_space", mi.tenant().config.max_address()},
		{"vm_main_memory",   mi.tenant().config.max_main_memory()},
		{"vm_bank_capacity", mi.machine().banked_memory_capacity_bytes()},
		{"vm_bank_highest",  mi.machine().banked_memory_allocated_bytes()},
		{"vm_bank_current",  mi.machine().banked_memory_bytes()},
		{"tasks_queued",   taskq.racy_queue_size()}
	});
}
static void gather_stats(VRT_CTX,
	nlohmann::json& j, TenantInstance* tenant)
{
	using namespace nlohmann;
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
		auto stats = gather_stats(*storage.storage_vm, prog->m_storage_queue);
		stats.push_back({"tasks_inschedule", prog->m_timer_system.racy_count()});

		obj["storage"] = {stats};
	}

	MachineStats totals {};
	auto& requests = obj["request"];
	auto machines = json::array();

	/* Individual request VMs */
	double total_resv_time = 0.0;
	for (size_t i = 0; i < prog->m_vms.size(); i++)
	{
		auto& mi = *prog->m_vms[i].mi;
		machines.push_back(gather_stats(mi, prog->m_vms[i].tp));

		totals.invocations += mi.stats().invocations;
		totals.exceptions += mi.stats().exceptions + mi.stats().exception_oom + mi.stats().exception_mem;
		totals.resets += mi.stats().resets;
		totals.full_resets += mi.stats().full_resets;
		totals.timeouts += mi.stats().timeouts;

		total_resv_time += mi.stats().reservation_time;
		totals.reservation_time += mi.stats().reservation_time;
		totals.vm_reset_time += mi.stats().vm_reset_time;
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

	requests["machines"] = std::move(machines);

	/* Cumulative totals */
	requests.push_back({"totals", {
		{"invocations", totals.invocations},
		{"resets",      totals.resets},
		{"resets_full", totals.full_resets},
		{"exceptions",  totals.exceptions},
		{"timeouts",    totals.timeouts},
		{"reservation_time",   totals.reservation_time},
		{"reset_time",         totals.vm_reset_time},
		{"request_cpu_time",   totals.request_cpu_time},
		{"exception_cpu_time", totals.error_cpu_time},
		{"input_bytes", totals.input_bytes},
		{"output_bytes",totals.output_bytes},
		{"status_2xx",  totals.status_2xx},
		{"status_3xx",  totals.status_3xx},
		{"status_4xx",  totals.status_4xx},
		{"status_5xx",  totals.status_5xx},
	}});

	std::string binary_type;
	if (prog->main_vm != nullptr) {
		binary_type = prog->main_vm->binary_type_string();
	} else {
		binary_type = "(not present)";
	}

	obj["program"] = {
		{"binary_type",  binary_type},
		{"binary_size",  prog->request_binary.size()},
		{"entry_points", {
			{"on_recv", prog->state.entry_address[(size_t)ProgramEntryIndex::ON_RECV]},
			{"backend_get", prog->state.entry_address[(size_t)ProgramEntryIndex::BACKEND_GET]},
			{"backend_post", prog->state.entry_address[(size_t)ProgramEntryIndex::BACKEND_POST]},
			{"backend_method", prog->state.entry_address[(size_t)ProgramEntryIndex::BACKEND_METHOD]},
			{"backend_stream", prog->state.entry_address[(size_t)ProgramEntryIndex::BACKEND_STREAM]},
			{"backend_error", prog->state.entry_address[(size_t)ProgramEntryIndex::BACKEND_ERROR]},
			{"live_update_serialize", prog->state.entry_address[(size_t)ProgramEntryIndex::LIVEUPD_SERIALIZE]},
			{"live_update_deserialize", prog->state.entry_address[(size_t)ProgramEntryIndex::LIVEUPD_DESERIALIZE]},
			{"socket_pause_resume_api", prog->state.entry_address[(size_t)ProgramEntryIndex::SOCKET_PAUSE_RESUME_API]}
		}},
		{"live_updates", prog->stats.live_updates},
		{"live_update_transfer_bytes", prog->stats.live_update_transfer_bytes},
		{"reservation_time",     total_resv_time},
		{"reservation_timeouts", prog->stats.reservation_timeouts},
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
#ifdef VARNISH_PLUS
		const char *error = "";
		int error_offset = 0;
		re = VRE_compile(pattern, 0, &error, &error_offset);
#else
		int error = 0;
		int error_offset = 0;
		re = VRE_compile(pattern, 0, &error, &error_offset, 0);
#endif

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
			const int matches =
				VRE_match(s.regex, program, strlen(program), 0, NULL);
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
