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
	  rspclient{nullptr}
{
	for (const auto* func : riscv_lookup_wishlist) {
		/* NOTE: We can't check if addr is 0 here, because
		   the wishlist applies to ALL machines. */
		const auto addr = lookup(func);
		sym_lookup.emplace(func, addr);
		const auto callsite = script.callsite(addr);
		sym_vector.push_back({func, addr, callsite.size});
	}
}
MachineInstance::~MachineInstance()
{
}

} // rvs
