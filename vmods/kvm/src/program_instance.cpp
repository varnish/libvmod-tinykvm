#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "utils/cpu_id.hpp"
#include "varnish.hpp"
#include <tinykvm/rsp_client.hpp>
extern "C" int gettid();

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
	for (auto& entry : instances) {
		delete entry.second;
	}
}

inst_pair ProgramInstance::workspace_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	auto* alloc = WS_Alloc(ctx->ws, sizeof(MachineInstance));
	MachineInstance *mi =
		new (alloc) MachineInstance{this->script, ctx, tenant, *this};
	mi->assign_instance(prog);
	return {mi, [] (void* inst) {
		auto* mi = (MachineInstance *)inst;
		mi->~MachineInstance();
	}};
}

inst_pair ProgramInstance::concurrent_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	queue_mtx.lock();
	auto& inst = instances[gettid()];
	queue_mtx.unlock();

	if (UNLIKELY(inst == nullptr)) {
		/* XXX: We can check the size without locking */
		const size_t max = tenant->config.max_machines();
		if (UNLIKELY(instances.size() >= max)) {
			return workspace_fork(ctx, tenant, prog);
		}
		/* Create a new machine instance on-demand */
		inst = new MachineInstance{this->script, ctx, tenant, *this};
	} else {
		/* The VM should already be reset, but needs a new VRT ctx */
		inst->set_ctx(ctx);
	}

	/* This creates a self-reference, which ensures that open
	   Machine instances will keep the program instance alive. */
	inst->assign_instance(prog);
	return {inst, [] (void* inst) {
		auto* mi = (MachineInstance *)inst;
		mi->reset_to(nullptr, mi->instance().script);
		mi->unassign_instance();
	}};
}

} // kvm
