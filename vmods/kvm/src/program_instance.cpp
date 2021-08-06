#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <tinykvm/rsp_client.hpp>
extern "C" int gettid();

namespace kvm {

/* Functions commonly exposed in all machines */
std::vector<const char*> lookup_wishlist {
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
	extern std::vector<const char*> lookup_wishlist;
	for (const auto* func : lookup_wishlist) {
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

long ProgramInstance::storage_call(tinykvm::Machine& src, gaddr_t func,
	gaddr_t src_addr, size_t src_size, gaddr_t res_addr, size_t res_size)
{
	uint64_t old_stack = storage.machine().stack_address();
	uint64_t new_stack  = (old_stack - src_size) & ~0x7;
	/* Detect wrap-around */
	if (UNLIKELY(new_stack > old_stack))
		return -1;
	if (UNLIKELY(res_addr + res_size < res_addr))
		return -1;

	std::unique_lock<std::mutex> lck (storage_mtx);

	auto& stm = storage.machine();
	/* Copy from the source machine into storage */
	stm.copy_from_machine(new_stack, src, src_addr, src_size);

	try {
		auto regs = stm.setup_call(func, new_stack,
			(uint64_t)new_stack, (uint64_t)src_size, (uint64_t)res_size);
		stm.set_registers(regs);
		stm.run();
		/* Get the result buffer and length (capped to res_size) */
		regs = stm.registers();
		const gaddr_t st_res_buffer = regs.rdi;
		const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
		/* Copy from the storage machine back into tenant VM instance */
		src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
		/* Run the function to the end, allowing cleanup */
		//stm.run();
		return st_res_size;
	} catch (...) {
		return -1;
	}
}

} // kvm


extern "C"
void kvm_cache_symbol(const char* symname)
{
	kvm::lookup_wishlist.push_back(symname);
}
