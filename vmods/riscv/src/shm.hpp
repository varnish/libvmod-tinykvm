#pragma once
#include "script_functions.hpp"

inline gaddr_t push_data(machine_t& machine,
	const char* data = nullptr, size_t len = 0)
{
	auto& sp = machine.cpu.reg(2);
	if (len == 0) return sp;

	/* Allocate the data on the stack */
	sp -= len;
	sp &= ~7; // 64-bit alignment
	machine.copy_to_guest(sp, data, len);
	return sp;
}
inline gaddr_t push_data(machine_t& m, const char* str) {
	return push_data(m, str, __builtin_strlen(str)+1);
}
inline gaddr_t push_data(machine_t& m, const std::string& str) {
	return push_data(m, str.c_str(), str.size()+1);
}
template <typename T>
inline gaddr_t push_data(machine_t& m, const T& data) {
	static_assert(std::is_standard_layout_v<T>, "Must be a POD type");
	static_assert(!std::is_pointer_v<T>, "Should not be a pointer");
	return push_data(m, (const char*) &data, sizeof(T));
}
