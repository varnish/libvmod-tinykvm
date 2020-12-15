#pragma once
#include "script_functions.hpp"

inline gaddr_t push_data(machine_t& machine,
	gaddr_t& iterator, const char* data = nullptr, size_t len = 0)
{
	if (len == 0) return iterator;

	/* Allocate the data on the stack */
	iterator -= len;
	iterator &= ~(gaddr_t) 0x7; // 64-bit alignment
	machine.copy_to_guest(iterator, data, len);
	return iterator;
}
inline gaddr_t push_data(machine_t& m,
	gaddr_t& iterator, const char* str)
{
	return push_data(m, iterator, str, __builtin_strlen(str)+1);
}
inline gaddr_t push_data(machine_t& m,
	gaddr_t& iterator, const std::string& str)
{
	return push_data(m, iterator, str.c_str(), str.size()+1);
}
template <typename T>
inline gaddr_t push_data(machine_t& m,
	gaddr_t& iterator, const T& data)
{
	static_assert(std::is_standard_layout_v<T>, "Must be a POD type");
	static_assert(!std::is_pointer_v<T>, "Should not be a pointer");
	return push_data(m, iterator, (const char*) &data, sizeof(T));
}
