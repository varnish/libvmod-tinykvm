#pragma once
#include <functional>
#include <memory>
#include "tenant.hpp"
struct vrt_ctx;

namespace kvm {
class MachineInstance;
class ProgramInstance;
struct VMPoolItem;

class TenantInstance {
public:
	VMPoolItem* vmreserve(const vrt_ctx*, bool debug);
	bool no_program_loaded() const noexcept { return this->program == nullptr; }

	uint64_t lookup(const char* name) const;

	void dynamic_call(uint32_t hash, MachineInstance&) const;

	/* Used by live update mechanism to replace the main VM with a new
	   one that was HTTP POSTed by a tenant while Varnish is running. */
	void commit_program_live(const vrt_ctx *,
		std::shared_ptr<ProgramInstance>& new_prog) const;

	/* If the tenants program employ serialization callbacks, we can
	   serialize the important bits of the current program and then
	   pass these bits to a new incoming live updated program, allowing
	   safe state transfer between storage VM of two programs. */
	static void serialize_storage_state(const vrt_ctx*,
		std::shared_ptr<ProgramInstance>& old,
		std::shared_ptr<ProgramInstance>& inst);

	TenantInstance(const vrt_ctx*, const TenantConfig&);
	long wait_for_initialization();

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	mutable std::shared_ptr<ProgramInstance> program = nullptr;
	/* Hot-swappable machine for debugging */
	mutable std::shared_ptr<ProgramInstance> debug_program = nullptr;
};

} // kvm
