#include "machine_instance.hpp"
#include <libriscv/rsp_server.hpp>

namespace rvs {

// functions available to all machines created during init
static const std::vector<const char*> riscv_lookup_wishlist {
	"on_init",
	"on_recv",
	"on_hash",
	"on_synth",
	"on_backend_fetch",
	"on_backend_response",
	"on_deliver",

	"on_live_update",
	"on_resume_update"
};

MachineInstance::MachineInstance(
	std::vector<uint8_t> elf,
	const vrt_ctx* ctx, SandboxTenant* vrm,
	bool debug)
	: binary{std::move(elf)},
	  script{binary, ctx, vrm, *this, false, debug},
	  storage{binary, ctx, vrm, *this, true, debug},
	  rspclient{nullptr},
	  sym_vector {}
{
	// sym_vector is now initialized, so we can run
	// through the main function of the tenants VM
	// for both the storage and main VM.
	storage.machine_initialize();
	script.machine_initialize();

	// For any VCL callback function that isn't set,
	// we will try to deduce the function by looking
	// up the name in the symbol table manually.
	for (size_t i = 0; i < riscv_lookup_wishlist.size(); i++) {
		const auto* func = riscv_lookup_wishlist[i];
		// NOTE: We can't fail if addr is 0x0 here, because
		// the wishlist applies to ALL machines. Most VCL
		// functions are optional.
		const auto addr = lookup(func);
		sym_lookup.emplace(func, addr);
		if (sym_vector.at(i).addr == 0x0) {
			const auto callsite = script.callsite(addr);
			sym_vector.at(i) = {func, addr, callsite.size};
		}
	}
}
MachineInstance::~MachineInstance()
{
}

} // rvs
