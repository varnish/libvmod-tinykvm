#include "machine_instance.hpp"
// functions available to all machines created during init
std::vector<const char*> riscv_lookup_wishlist {
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
	const vrt_ctx* ctx, vmod_riscv_machine* vrm,
	bool debug)
	: binary{std::move(elf)},
	  storage{binary, ctx, vrm, *this, true, debug},
	  script{binary, ctx, vrm, *this, false, debug}
{
	extern std::vector<const char*> riscv_lookup_wishlist;
	for (const auto* func : riscv_lookup_wishlist) {
		/* NOTE: We can't check if addr is 0 here, because
		   the wishlist applies to ALL machines. */
		const auto addr = lookup(func);
		sym_lookup.emplace(strdup(func), addr);
		const auto callsite = script.callsite(addr);
		sym_vector.push_back({func, addr, callsite.size});
	}
}
MachineInstance::~MachineInstance()
{
}
