#include "script.hpp"

#include "crc32.hpp"
#include <include/syscall_helpers.hpp>
#include "machine/include_api.hpp"

extern "C" {
# include <vre.h>
}

static const bool TRUSTED_CALLS = true;
riscv::Page Script::g_hidden_stack;

void Script::init()
{
	// the hidden area is read-only for the guest
	g_hidden_stack.attr.write  = false;
}

Script::Script(
	const riscv::Machine<riscv::RISCV32>& source, const vrt_ctx* ctx,
	uint64_t insn, uint64_t mem, uint64_t heap)
	: m_machine(source.memory.binary(), {
		.memory_max = mem,  .owning_machine = &source
	  }),
	  m_ctx(ctx), m_max_instructions(insn),
	  m_max_memory(mem), m_max_heap(heap)
{
	this->m_crashed = !this->machine_initialize();
}

Script::~Script()
{
	// free any unfreed regex pointers
	for (auto& entry : m_regex_cache)
		if (entry.re) VRE_free(&entry.re);
}

void Script::setup_virtual_memory()
{
	const int heap_pageno   = 0x40000000 >> riscv::Page::SHIFT;
	const int stack_pageno  = heap_pageno - 1;
	auto& mem = machine().memory;
	mem.set_stack_initial((uint32_t) stack_pageno << riscv::Page::SHIFT);
	// this separates heap and stack
	mem.install_shared_page(stack_pageno, riscv::Page::guard_page());
	// install a hidden area that the internal APIs use
	mem.install_shared_page(HIDDEN_AREA >> riscv::Page::SHIFT, g_hidden_stack);
}

bool Script::machine_initialize()
{
	// setup system calls and traps
	this->machine_setup(machine());
	// run through the initialization
	try {
		machine().simulate(m_max_instructions);

		if (UNLIKELY(machine().cpu.instruction_counter() >= m_max_instructions)) {
			printf(">>> Exception: Ran out of instructions\n");
			return false;
		}
	} catch (riscv::MachineException& me) {
		printf(">>> Machine exception %d: %s (data: %d)\n",
				me.type(), me.what(), me.data());
#ifdef RISCV_DEBUG
		machine().print_and_pause();
#endif
		return false;
	} catch (std::exception& e) {
		printf(">>> Exception: %s\n", e.what());
		return false;
	}
    return true;
}
void Script::machine_setup(riscv::Machine<riscv::RISCV32>& machine)
{
	machine.set_userdata<Script>(this);
	const uint32_t exit_addr = machine.address_of("exit");
	if (exit_addr)
		machine.memory.set_exit_address(exit_addr);
	else
		throw std::runtime_error("The binary is missing a public exit function!");
	// page protections and "hidden" stacks
	this->setup_virtual_memory();
	// add system call interface
	auto* arena = setup_native_heap_syscalls<4>(machine, m_max_heap);
	setup_native_memory_syscalls<4>(machine, TRUSTED_CALLS);
    setup_syscall_interface(machine);
	machine.on_unhandled_syscall(
		[] (int number) {
			printf("Unhandled system call: %d\n", number);
		});

	// create execute trapping syscall page
	// this is the last page in the 32-bit address space
	auto& page = machine.memory.create_page(0xFFFFF);
	// create an execution trap on the page
	page.set_trap(
		[&machine] (riscv::Page&, uint32_t sysn, int, int64_t) -> int64_t {
			// invoke a system call
			machine.system_call(1024 - sysn / 4);
			// return to caller
			const auto retaddr = machine.cpu.reg(riscv::RISCV::REG_RA);
			machine.cpu.jump(retaddr);
			return 0;
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

uint32_t Script::resolve_address(const std::string& name) const {
	return machine().address_of(name.c_str());
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
