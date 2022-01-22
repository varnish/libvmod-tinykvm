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

	/* Used by vmcommit system call to replace the main VM with a new
	   one based on the current active VM, including from a request VM
	   or a storage VM. */
	void commit_program_live(
		std::shared_ptr<ProgramInstance>& new_prog, bool storage) const;

	/* If the tenants program employ serialization callbacks, we can
	   serialize the important bits of the current program and then
	   pass these bits to a new incoming live updated program, allowing
	   safe state transfer between storage VM of two programs. */
	static void serialize_storage_state(const vrt_ctx*,
		std::shared_ptr<ProgramInstance>& old,
		std::shared_ptr<ProgramInstance>& inst);

	TenantInstance(const vrt_ctx*, const TenantConfig&);

	/* Initialized during vcl_init */
	const uint64_t magic = 0xb385716f486938e6;
	const TenantConfig config;
	/* Hot-swappable machine */
	mutable std::shared_ptr<ProgramInstance> program = nullptr;
	/* Hot-swappable machine for debugging */
	mutable std::shared_ptr<ProgramInstance> debug_program = nullptr;
};

} // kvm
