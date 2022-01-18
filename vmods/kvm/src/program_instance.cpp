#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "gettid.hpp"
#include "varnish.hpp"
#include <cstring>
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
	  script{std::make_shared<MachineInstance>(binary, ctx, ten, this, false, debug)},
	  storage{binary, ctx, ten, this, true, debug},
	  rspclient{nullptr}
{
	if (!script->is_waiting_for_requests()) {
		throw std::runtime_error("The main program was not waiting for requests. Did you forget to call 'wait_for_requests()'?");
	}
	if (!storage.is_waiting_for_requests()) {
		throw std::runtime_error("The storage program was not waiting for requests. Did you forget to call 'wait_for_requests()'?");
	}

	extern std::vector<const char*> lookup_wishlist;
	for (const auto* func : lookup_wishlist) {
		/* NOTE: We can't check if addr is 0 here, because
		   the wishlist applies to ALL machines. */
		const auto addr = script->resolve_address(func);
		sym_lookup.emplace(func, addr);
	}
}
ProgramInstance::~ProgramInstance()
{
}

uint64_t ProgramInstance::lookup(const char* name) const
{
	// hash table
	const auto& it = sym_lookup.find(name);
	if (it != sym_lookup.end()) return it->second;

	// slow fallback
	return script->resolve_address(name);
}
ProgramInstance::gaddr_t ProgramInstance::entry_at(const int idx) const
{
	return entry_address.at(idx);
}
void ProgramInstance::set_entry_at(const int idx, gaddr_t addr)
{
	entry_address.at(idx) = addr;
}

inst_pair ProgramInstance::concurrent_fork(const vrt_ctx* ctx,
	TenantInstance* tenant, std::shared_ptr<ProgramInstance>& prog)
{
	thread_local MachineInstance* inst = nullptr;
	auto script_ref = script;
	if (UNLIKELY(inst == nullptr)) {
		/* Create a new machine instance on-demand */
		inst = new MachineInstance(script_ref, ctx, tenant, this);
	} else {
		/* The VM needs to be reset and get a new VRT ctx */
		inst->reset_to(ctx, script_ref);
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
	size_t n, VirtBuffer buffers[], gaddr_t res_addr, size_t res_size)
{
	/* Detect wrap-around */
	if (UNLIKELY(res_addr + res_size < res_addr))
		return -1;

	auto future = m_storage_queue.enqueue(
	[&] () -> long
	{
		auto& stm = storage.machine();
		uint64_t vaddr = storage.machine().stack_address();

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
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			constexpr uint32_t ONE_SECOND = 62'500'000;
			const uint32_t ticks = ONE_SECOND
#else
			const uint32_t ticks = 0;
#endif
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, ticks, new_stack,
				(uint64_t)n, (uint64_t)stm_bufaddr, (uint64_t)res_size);
			stm.set_registers(regs);
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			stm.run();
#else
			stm.run(1.0);
#endif
			/* Get the result buffer and length (capped to res_size) */
			regs = stm.registers();
			const gaddr_t st_res_buffer = regs.rdi;
			const uint64_t st_res_size  = (regs.rsi < res_size) ? regs.rsi : res_size;
			if (res_addr != 0x0 && st_res_buffer != 0x0) {
				/* Copy from the storage machine back into tenant VM instance */
				src.copy_from_machine(res_addr, stm, st_res_buffer, st_res_size);
			}
			/* Run the function to the end, allowing cleanup */
			stm.run(1.0);
			return st_res_size;
		} catch (...) {
			return -1;
		}
	});
	return future.get();
}

long ProgramInstance::async_storage_call(gaddr_t func, gaddr_t arg)
{
	// Block and finish previous async tasks
	// TODO: Read the return value of the tasks to detect errors
	m_async_tasks.clear();

	m_async_tasks.push_back(
		m_storage_queue.enqueue(
	[=] () -> long
	{
		auto& stm = storage.machine();
		const uint64_t new_stack = storage.machine().stack_address();

		/* This vmcall has no attached VRT_CTX. */
		storage.set_ctx(nullptr);

		try {
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			constexpr uint32_t ONE_SECOND = 62'500'000;
			const uint32_t ticks = ONE_SECOND
#else
			const uint32_t ticks = 0;
#endif
			tinykvm::tinykvm_x86regs regs;
			stm.setup_call(regs, func, ticks, new_stack,
				(uint64_t)arg);
			stm.set_registers(regs);
#ifdef TINYKVM_FAST_EXECUTION_TIMEOUT
			stm.run();
#else
			stm.run(1.0);
#endif
			return 0;
		} catch (...) {
			return -1;
		}
	}));
	return 0;
}

long ProgramInstance::live_update_call(const vrt_ctx* ctx,
	gaddr_t func, ProgramInstance& new_prog, gaddr_t newfunc)
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
			storage.set_ctx(ctx);
			auto& old_machine = storage.machine();
			old_machine.timed_vmcall(func,
				storage.tenant().config.max_time());
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

	SerializeResult res = future.get();
	if (res.data == nullptr)
		return -1;

	auto& new_machine = new_prog.storage.machine();
	/* Begin resume procedure */
	new_prog.storage.set_ctx(ctx);
	new_machine.timed_vmcall(newfunc,
		new_prog.storage.tenant().config.max_time(),
		(uint64_t)res.len);
	auto new_regs = new_machine.registers();
	/* The machine should be calling STOP with rsi=dst_data */
	auto res_data = new_regs.rdi;
	auto res_size = std::min(new_regs.rsi, res.len);
	if (res_data != 0x0) { // Just a courtesy, we *do* check permissions.
		new_machine.copy_to_guest(
			res_data, res.data.get(), res_size);
		/* Resume the new machine, allowing it to deserialize data */
		new_machine.run();
	}
	return 0;
}

void ProgramInstance::commit_instance_live(
	std::shared_ptr<MachineInstance>& new_inst)
{
	/* Make a reference to the current program, keeping it alive */
	std::shared_ptr<MachineInstance> current = this->script;

	std::atomic_exchange(&this->script, new_inst);
}

} // kvm


extern "C"
void kvm_cache_symbol(const char* symname)
{
	kvm::lookup_wishlist.push_back(symname);
}
