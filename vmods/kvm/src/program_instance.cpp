#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "gettid.hpp"
#include "varnish.hpp"
#include <tinykvm/rsp_client.hpp>

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
	  script{binary, ctx, ten, this, false, debug},
	  storage{binary, ctx, ten, this, true, debug},
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
}

inst_pair ProgramInstance::workspace_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	auto* alloc = WS_Alloc(ctx->ws, sizeof(MachineInstance));
	MachineInstance *mi =
		new (alloc) MachineInstance{this->script, ctx, tenant, this};
	mi->assign_instance(prog);
	return {mi, [] (void* inst) {
		auto* mi = (MachineInstance *)inst;
		mi->~MachineInstance();
	}};
}

inst_pair ProgramInstance::concurrent_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	thread_local MachineInstance* inst = nullptr;
	if (UNLIKELY(inst == nullptr)) {
		/* Create a new machine instance on-demand */
		inst = new MachineInstance(this->script, ctx, tenant, this);
	} else {
		/* The VM needs to be reset and get a new VRT ctx */
		inst->reset_to(ctx, this->script);
	}

	/* This creates a self-reference, which ensures that open
	   Machine instances will keep the program instance alive. */
	inst->assign_instance(prog);
	/* What happens when the transaction is done */
	return {inst, [] (void* inst) {
		auto* mi = (MachineInstance *)inst;
		mi->tail_reset();
		mi->unassign_instance();
	}};
}

long ProgramInstance::storage_call(tinykvm::Machine& src, gaddr_t func,
	size_t n, VirtBuffer buffers[n], gaddr_t res_addr, size_t res_size)
{
	uint64_t old_stack = storage.machine().stack_address();

	/* Detect wrap-around */
	if (UNLIKELY(res_addr + res_size < res_addr))
		return -1;

	auto future = m_storage_queue.enqueue(
	[&] () -> long
	{
		auto& stm = storage.machine();
		uint64_t vaddr = old_stack;

		for (size_t i = 0; i < n; i++) {
			vaddr -= buffers[i].len;
			vaddr &= ~(uint64_t)0x7;
			stm.copy_from_machine(vaddr, src, buffers[i].addr, buffers[i].len);
			buffers[i].addr = vaddr;
		}

		vaddr -= n * sizeof(VirtBuffer);
		const uint64_t stm_bufaddr = vaddr;
		stm.copy_to_guest(stm_bufaddr, buffers, n * sizeof(VirtBuffer));

		const uint64_t new_stack = vaddr;

		/* We need to use the CTX from the current program */
		auto& inst = *src.get_userdata<MachineInstance>();
		storage.set_ctx(inst.ctx());

		try {
			auto regs = stm.setup_call(func, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			stm.set_registers(regs);
			stm.run();
			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			/* Copy from the storage machine back into tenant VM instance */
			src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
			/* Run the function to the end, allowing cleanup */
			stm.run();
			return st_res_size;
		} catch (...) {
			return -1;
		}
	});
	return future.get();
}

long ProgramInstance::live_update_call(
	gaddr_t func, tinykvm::Machine& new_machine, gaddr_t newfunc)
{
	struct SerializeResult {
		std::unique_ptr<char[]>  data;
		unsigned long long len;
	};
	auto future = m_storage_queue.enqueue(
	[&] () -> SerializeResult
	{
		try {
			/* Serialize data in the old machine */
			auto& old_machine = storage.machine();
			old_machine.vmcall(func);
			/* Get serialized data */
			auto regs = old_machine.registers();
			auto data_addr = regs.rdi;
			auto data_len  = regs.rsi;
			if (data_addr + data_len < data_addr) {
				return {nullptr, 0};
			}
			char* data = new char[data_len];
			old_machine.copy_from_guest(data, data_addr, data_len);
			return {std::unique_ptr<char[]> (data), data_len};
		} catch (...) {
			/* We have to make sure Varnish is not taken down */
			return {nullptr, 0};
		}
	});
	auto res = future.get();
	if (res.data == nullptr)
		return -1;
	/* Begin resume procedure */
	new_machine.vmcall(newfunc, (uint64_t)res.len);
	auto new_regs = new_machine.registers();
	/* The machine should be calling STOP with rsi=dst_data */
	auto res_data = new_regs.rdi;
	auto res_size = std::min(new_regs.rsi, res.len);
	new_machine.copy_to_guest(
		res_data, res.data.get(), res_size);
	/* Resume the new machine, allowing it to deserialize data */
	new_machine.run();
	return 0;
}

} // kvm


extern "C"
void kvm_cache_symbol(const char* symname)
{
	kvm::lookup_wishlist.push_back(symname);
}
