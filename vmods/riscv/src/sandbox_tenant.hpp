#pragma once
#include "script.hpp"
#include "tenant.hpp"
#include "machine_instance.hpp"
#define SCRIPT_MAGIC 0x83e59fa5

namespace rvs {

struct SandboxTenant
{
	using ghandler_t = std::function<void(Script&)>;

	Script* vmfork(const vrt_ctx*, bool debug);
	bool no_program_loaded() const noexcept { return this->program == nullptr; }

	SandboxTenant(const vrt_ctx*, const TenantConfig&);
	static void init();
	void init_vmods(const vrt_ctx*);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable program */
	std::shared_ptr<MachineInstance> program = nullptr;
	/* Program for debugging */
	std::shared_ptr<MachineInstance> debug_program = nullptr;
};

} // rvs
