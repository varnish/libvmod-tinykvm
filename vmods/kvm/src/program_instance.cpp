#include "program_instance.hpp"
#include <tinykvm/rsp_client.hpp>
/* Functions commonly exposed in all machines */
std::vector<const char*> kvm_lookup_wishlist {
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

ProgramInstance::ProgramInstance(
	std::vector<uint8_t> elf,
	const vrt_ctx* ctx, TenantInstance* ten,
	bool debug)
	: binary{std::move(elf)},
	  script{binary, ctx, ten, *this, false, debug},
	  storage{binary, ctx, ten, *this, true, debug},
	  rspclient{nullptr}
{
	extern std::vector<const char*> kvm_lookup_wishlist;
	for (const auto* func : kvm_lookup_wishlist) {
		/* NOTE: We can't check if addr is 0 here, because
		   the wishlist applies to ALL machines. */
		const auto addr = lookup(func);
		sym_lookup.emplace(func, addr);
	}
}
ProgramInstance::~ProgramInstance()
{
}
