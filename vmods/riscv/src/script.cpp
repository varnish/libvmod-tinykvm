#include "script.hpp"

#include "crc32.hpp"
#include <include/syscall_helpers.hpp>
#include "machine/include_api.hpp"
#include "varnish.hpp"

static const bool TRUSTED_CALLS = true;
static constexpr int HEAP_PAGENO   = 0x40000000 >> riscv::Page::SHIFT;
static constexpr int STACK_PAGENO  = HEAP_PAGENO - 1;


Script::Script(
	const Script& source, const vrt_ctx* ctx, struct vmod_riscv_machine* vrm)
	: m_machine(source.machine().memory.binary(), {
		.memory_max = source.m_max_memory,
		.owning_machine = &source.machine()
	  }),
	  m_ctx(ctx), m_vrm(vrm),
	  m_name(source.m_name),
	  m_max_instructions(source.m_max_instructions),
	  m_max_heap(source.m_max_heap),
	  m_max_memory(source.m_max_memory)
{
	/* No initialization */
	this->machine_setup(machine(), false);

	/* Transfer data from the old arena, to fully replicate heap */
	arena_transfer((sas_alloc::Arena*) source.m_arena, (sas_alloc::Arena*) m_arena);
	/* Load the compiled regexes of the source */
	for (auto& regex : source.m_regex_cache) {
		m_regex_cache.push_back(regex);
		m_regex_cache.back().non_owned = true;
	}
}

Script::Script(
	const std::vector<uint8_t>& binary,
	const vrt_ctx* ctx, const char* name,
	uint64_t insn, uint64_t mem, uint64_t heap)
	: m_machine(binary, { .memory_max = mem }), m_ctx(ctx), m_name(name),
	  m_max_instructions(insn), m_max_heap(heap), m_max_memory(mem)
{
	this->machine_initialize(true);
}

Script::~Script()
{
	// free any unfreed regex pointers
	for (auto& entry : m_regex_cache)
		if (entry.re && !entry.non_owned)
			VRE_free(&entry.re);
}

void Script::setup_virtual_memory()
{
	using namespace riscv;
	auto& mem = machine().memory;
	mem.set_stack_initial(STACK_PAGENO * Page::size());
	// this separates heap and stack
	mem.install_shared_page(STACK_PAGENO, Page::guard_page());

	auto* ws = ctx()->ws;
	new (&ro_area) MemArea<4> (machine(), RO_AREA_BEGIN, RO_AREA_END,
		{ .write = false, .non_owning = true }, ws);
	mem.install_shared_page(RO_AREA_END >> 12, Page::guard_page());
	new (&rw_area) MemArea<4> (machine(), RW_AREA_BEGIN, RW_AREA_END,
		{ .write = true, .non_owning = true }, ws);
	mem.install_shared_page(RW_AREA_END >> 12, Page::guard_page());
}

bool Script::machine_initialize(bool init)
{
	// setup system calls and traps
	this->machine_setup(machine(), init);
	// run through the initialization
	if (init) {
		try {
			machine().simulate(m_max_instructions);

			if (UNLIKELY(machine().cpu.instruction_counter() >= m_max_instructions)) {
				printf(">>> Exception: Ran out of instructions\n");
				return false;
			}
		} catch (riscv::MachineException& me) {
			printf(">>> Machine exception %d: %s (data: %#x)\n",
					me.type(), me.what(), me.data());
#ifdef RISCV_DEBUG
			machine().print_and_pause();
#endif
			return false;
		} catch (std::exception& e) {
			printf(">>> Exception: %s\n", e.what());
			return false;
		}
	}
    return true;
}
void Script::machine_setup(riscv::Machine<riscv::RISCV32>& machine, bool init)
{
	machine.set_userdata<Script>(this);

	machine.memory.set_page_fault_handler(
		[this] (auto& mem, size_t pageno) -> riscv::Page& {
			/* Pages are allocated from workspace */
			//printf("Creating page %zu @ 0x%X\n", pageno, pageno * 4096);
			bool dont_fork = false;
			if (pageno >= STACK_PAGENO-128 && pageno < STACK_PAGENO) {
				dont_fork = true;
			}
			auto* data =
				(riscv::PageData*) WS_Alloc(m_ctx->ws, riscv::Page::size());
			if (LIKELY(data != nullptr)) {
				return mem.allocate_page(pageno,
					riscv::PageAttributes{
						.non_owning = true, // don't delete!
						.dont_fork  = dont_fork,
						.user_defined = 1 },
					data);
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory", mem.pages_total());
		});
	machine.memory.set_page_write_handler(
		[this] (auto& mem, riscv::Page& page) -> void {
			assert(page.has_data() && page.attr.is_cow);
			/* Pages are allocated from workspace */
			auto* data =
				(riscv::PageData*) WS_Copy(m_ctx->ws, page.data(), page.size());
			if (LIKELY(data != nullptr)) {
				/* Release any already non-owned data */
				if (page.attr.non_owning)
					page.m_page.release();
				page.attr.is_cow = false;
				page.attr.non_owning = true; /* Avoid calling delete */
				page.m_page.reset(data);
				return;
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory", mem.pages_total());
		});

	// page protections and "hidden" stacks
	this->setup_virtual_memory();
	// stack
	machine.cpu.reset_stack_pointer();

	if (init)
	{
		const uint32_t exit_addr = machine.address_of("exit");
		if (exit_addr)
			machine.memory.set_exit_address(exit_addr);
		else
			throw std::runtime_error("The binary is missing a public exit function!");
		/* Newlib expects to find arguments on stack */
		machine.setup_argv({ name() });
	}

	// add system call interface
	this->m_arena = setup_native_heap_syscalls<4>(machine, m_max_heap);
	setup_native_memory_syscalls<4>(machine, TRUSTED_CALLS);
    setup_syscall_interface(machine);

	machine.on_unhandled_syscall(
		[this] (int number) {
			VSLb(m_ctx->vsl, SLT_Debug,
				"VM unhandled system call: %d\n", number);
		});
}
void Script::handle_exception(uint32_t address)
{
	try {
		throw;
	}
	catch (const riscv::MachineException& e) {
		fprintf(stderr, "Script exception: %s (data: %#x)\n", e.what(), e.data());
		fprintf(stderr, ">>> Machine registers:\n[PC\t%08X] %s\n",
			machine().cpu.pc(),
			machine().cpu.registers().to_string().c_str());
	}
	catch (const std::exception& e) {
		fprintf(stderr, "Script exception: %s\n", e.what());
	}
	printf("Program page: %s\n", machine().memory.get_page_info(machine().cpu.pc()).c_str());
	printf("Stack page: %s\n", machine().memory.get_page_info(machine().cpu.reg(2)).c_str());

	auto callsite = machine().memory.lookup(address);
	fprintf(stderr, "Function call: %s\n", callsite.name.c_str());
	this->print_backtrace(address);
	this->m_crashed = true;
}
void Script::handle_timeout(uint32_t address)
{
	this->m_budget_overruns ++;
	auto callsite = machine().memory.lookup(address);
	fprintf(stderr, "Script hit max instructions for: %s"
		" (Overruns: %d)\n", callsite.name.c_str(), m_budget_overruns);
	/* Maybe not always true, but ... */
	this->m_crashed = true;
}
void Script::print_backtrace(const uint32_t addr)
{
	machine().memory.print_backtrace(
		[] (const char* buffer, size_t len) {
			printf("-> %.*s\n", (int)len, buffer);
		});
	auto origin = machine().memory.lookup(addr);
	printf("-> [-] 0x%08x + 0x%.3x: %s\n",
			origin.address, origin.offset, origin.name.c_str());
}

uint32_t Script::guest_alloc(size_t len)
{
	return arena_malloc((sas_alloc::Arena*) m_arena, len);
}

uint32_t Script::resolve_address(const char* name) const {
	return machine().address_of(name);
}
std::string Script::symbol_name(uint32_t address) const
{
	auto callsite = machine().memory.lookup(address);
	return callsite.name;
}

int Script::regex_find(uint32_t hash) const
{
	for (unsigned idx = 0; idx < m_regex_cache.size(); idx++) {
		if (m_regex_cache[idx].hash == hash) return idx;
	}
	return -1;
}
size_t Script::regex_manage(struct vre* ptr, uint32_t hash)
{
	m_regex_cache.push_back({ptr, hash});
	return m_regex_cache.size() - 1;
}
void Script::regex_free(size_t idx)
{
	m_regex_cache.at(idx) = { nullptr, 0 };
}
