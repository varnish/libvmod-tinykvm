#pragma once
#include "script.hpp"

#include <type_traits>
#include <libriscv/util/crc32.hpp>

namespace rvs {

using machine_t = Script::machine_t;
using gaddr_t = Script::gaddr_t;

#define APICALL(func) static void func(riscv::Machine<Script::MARCH>& machine [[maybe_unused]])

inline Script& get_script(machine_t& m) noexcept {
	return *m.get_userdata<Script> ();
}

inline const auto* get_ctx(machine_t& m) noexcept {
	return get_script(m).ctx();
}

template <typename... T>
constexpr auto make_array(T&&... t) -> std::array<std::common_type_t<T...>, sizeof...(t)>
{
	return {std::forward<T>(t)...};
}

inline uint64_t mirror_stack_call(machine_t& machine, Script& remote, gaddr_t func)
{
	// scan registers to determine if we need to mirror the stack
	bool mirror_stack = false;
	const gaddr_t STACK_BEG = machine.memory.stack_initial();
	const gaddr_t STACK_TOP = machine.cpu.reg(2) & ~(gaddr_t) 0xFFF;

	auto& regs = remote.machine().cpu.registers();
	for (int i = 10; i < 16; i++) {
		if (regs.get(i) >= STACK_TOP && regs.get(i) < STACK_BEG) {
			mirror_stack = true;
			break;
		}
	}
	std::vector<riscv::Page*> mounted_pages;
	if (mirror_stack)
	{
		// mount the stack of the calling machine as shared memory
		for (gaddr_t src = STACK_TOP; src < STACK_BEG; src += 0x1000)
		{
			const auto& page = machine.memory.get_page(src);
			auto& new_page =
				remote.machine().memory.install_shared_page(src >> 12, page);
			mounted_pages.push_back(&new_page);
		}
	}
	remote.machine().reset_instruction_counter();
	// actual VM function call
	remote.call(func);
	// make all the mounted pages read-only as "cleanup"
	for (auto* page : mounted_pages) {
		page->attr.write = false;
	}
	return remote.machine().instruction_counter();
}

} // rvs
