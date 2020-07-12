#pragma once
#include <libriscv/machine.hpp>

using machine_t = riscv::Machine<4>;

/* TODO: prevent too much recursion
   TODO: prevent stack overflow     */

struct SharedMemoryArea
{
	uint32_t push(const std::string& str) {
		dst -= str.size()+1;
		dst &= ALIGN_MASK; // maintain alignment
		machine.memory.memcpy(dst, str.c_str(), str.size()+1);
		return dst;
	}
	uint32_t push(const char* str) {
		const size_t len = __builtin_strlen(str)+1;
		dst = (dst - len) & ALIGN_MASK;
		machine.memory.memcpy(dst, str, len);
		return dst;
	}
	uint32_t push(const char* str, size_t len) {
		dst = (dst - len) & ALIGN_MASK;
		machine.memory.memcpy(dst, str, len);
		return dst;
	}
	template <typename T>
	uint32_t push(const T& data) {
		static_assert(std::is_standard_layout_v<T>, "Must be a POD type");
		static_assert(!std::is_pointer_v<T>, "Should not be a pointer");
		dst = (dst - sizeof(T)) & ALIGN_MASK;
		machine.memory.memcpy(dst, &data, sizeof(T));
		return dst;
	}
	template <typename T>
	uint32_t write(const T& data) {
		static_assert(std::is_standard_layout_v<T>, "Must be a POD type");
		static_assert(!std::is_pointer_v<T>, "Should not be a pointer");
		auto ret = dst;
		machine.memory.memcpy(dst, &data, sizeof(T));
		dst += sizeof(T);
		return ret;
	}

	uint32_t address() const noexcept { return dst; }
	void     set(uint32_t addr) { dst = addr; }

	SharedMemoryArea(Script& script)
		: machine(script.machine()), dst(script.shm_address()), stored_dst(dst) {}
	SharedMemoryArea(Script& script, uint32_t& addr)
		: machine(script.machine()), dst(addr), stored_dst(addr) {}
	/* When this gets destroyed, we restore the stack to its original value */
	~SharedMemoryArea() {
		dst = stored_dst;
	}

private:
	static constexpr uint32_t ALIGN_MASK = ~0x3;
	machine_t& machine;
	uint32_t& dst;
	uint32_t  stored_dst;
};
