#pragma once
#include <functional>
#include <memory>
#include "tenant.hpp"
struct vrt_ctx;

namespace kvm {
class MachineInstance;
class ProgramInstance;

class TenantInstance {
public:
	MachineInstance* vmfork(const vrt_ctx*, bool debug);
	bool no_program_loaded() const noexcept { return this->program == nullptr; }

	uint64_t lookup(const char* name) const;

	void dynamic_call(uint32_t hash, MachineInstance&) const;

	TenantInstance(const vrt_ctx*, const TenantConfig&);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	std::shared_ptr<ProgramInstance> program = nullptr;
	/* Machine for debugging */
	std::shared_ptr<ProgramInstance> debug_program = nullptr;
};

} // kvm
