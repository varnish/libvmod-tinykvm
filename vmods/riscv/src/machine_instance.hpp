#pragma once
#include "script.hpp"
#include <mutex>
namespace riscv {
	template <int W> struct RSPClient;
}

namespace rvs {

[[maybe_unused]]
static inline std::array<const char*, 12> callback_names = {
	"Invalid callback",
	"on_recv",
	"on_hash",
	"on_synth",         // 3
	"on_backend_fetch",
	"on_backend_response",
	"on_backend_error",
	"on_deliver",
	"on_hit",
	"on_miss",
	"on_live_update",   // 10
	"on_resume_update", // 11
};

struct MachineInstance
{
	MachineInstance(std::vector<uint8_t>,
		const vrt_ctx*, SandboxTenant*, bool = false);
	~MachineInstance();

	const std::vector<uint8_t> binary;
	Script   script;

	Script   storage;
	std::mutex storage_mtx;

	std::unique_ptr<riscv::RSPClient<Script::MARCH>> rspclient;
	Script* rsp_script = nullptr;
	std::mutex rsp_mtx;

	std::array<Script::gaddr_t, 12> callback_entries;
};

} // rvs
