#pragma once
#include <type_traits>
#include "script.hpp"
#include "shm.hpp"

using machine_t = riscv::Machine<4>;

#define APICALL(func) static long func(machine_t& machine [[maybe_unused]])

inline const auto* get_ctx(machine_t& m) {
	return m.get_userdata<Script> ()->ctx();
}

inline auto& get_script(machine_t& m) {
	return *m.get_userdata<Script> ();
}
