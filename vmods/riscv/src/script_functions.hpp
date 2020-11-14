#pragma once
#include "script.hpp"

#include <type_traits>
#include "crc32.hpp"
#include "shm.hpp"

using machine_t = Script::machine_t;
using gaddr_t = Script::gaddr_t;

#define APICALL(func) static void func(machine_t& machine [[maybe_unused]])

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
