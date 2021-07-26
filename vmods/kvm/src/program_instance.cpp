#include "program_instance.hpp"
#include "utils/cpu_id.hpp"
#include "varnish.hpp"
#include <tinykvm/rsp_client.hpp>

namespace kvm {

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

MachineInstance* ProgramInstance::workspace_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	auto* alloc = WS_Alloc(ctx->ws, sizeof(MachineInstance));
	MachineInstance *mi =
		new (alloc) MachineInstance{this->script, ctx, tenant, *this};
	mi->assign_instance(prog);
	return mi;
}
void ProgramInstance::workspace_free(MachineInstance* inst)
{
	inst->~MachineInstance();
}

thread_local std::vector<MachineInstance*> vq;

MachineInstance* ProgramInstance::concurrent_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	if (UNLIKELY(vq.empty())) {
		/* When the queue is empty, just create a new machine instance */
		auto* inst = new MachineInstance{this->script, ctx, tenant, *this};
		/* This creates a self-reference, which ensures that open
		   Machine instances will keep the program instance alive. */
		inst->assign_instance(prog);
		return inst;
	}

	auto* inst = vq.back();
	vq.pop_back();

	inst->reset_to(ctx, this->script);
	inst->assign_instance(prog);
	return inst;
}
void ProgramInstance::return_machine(MachineInstance* inst)
{
	inst->unassign_instance();
	vq.push_back(inst);
}

} // kvm
