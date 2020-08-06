#pragma once
#include <cassert>
#include <functional>
#include <libriscv/machine.hpp>
#include <EASTL/fixed_vector.h>
#include "memarea.hpp"
struct vrt_ctx;
struct vmod_riscv_machine;
struct MachineInstance;

class Script {
public:
	static constexpr int MARCH = riscv::RISCV64;
	using gaddr_t = riscv::address_type<MARCH>;
	using machine_t = riscv::Machine<MARCH>;
	static constexpr gaddr_t RO_AREA_BEGIN = 0x10000;
	static constexpr gaddr_t RO_AREA_END   = 0x11000;
	static constexpr gaddr_t RW_AREA_BEGIN = 0x12000;
	static constexpr gaddr_t RW_AREA_END   = 0x13000;

	// call any script function, with any parameters
	template <typename... Args>
	inline long call(gaddr_t addr, Args&&...);

	template <typename... Args>
	inline long preempt(gaddr_t addr, Args&&...);

	auto& machine() { return m_machine; }
	const auto& machine() const { return m_machine; }

	const auto* ctx() const noexcept { return m_ctx; }
	const auto* vrm() const noexcept { return m_vrm; }
	const auto& instance() const noexcept { return m_inst; }

	uint64_t max_instructions() const noexcept;
	const char* name() const noexcept;
	auto* want_result() const noexcept { return m_want_result.c_str(); }
	int want_status() const noexcept { return m_want_status; }
	void set_result(const std::string& res, int status) {
		m_want_result = res; m_want_status = status;
	}

	gaddr_t guest_alloc(size_t len);
	auto& shm_address() { return m_shm_address; }

	MemArea<MARCH> ro_area;
	MemArea<MARCH> rw_area;

	int    regex_find(uint32_t hash) const;
	size_t regex_manage(struct vre*, uint32_t hash);
	void   regex_free(size_t);
	struct vre* regex_get(size_t idx) { return m_regex_cache.at(idx).re; }

	std::string symbol_name(gaddr_t address) const;
	gaddr_t resolve_address(const char* name) const;
	auto    callsite(gaddr_t addr) const { return machine().memory.lookup(addr); }

	void print_backtrace(const gaddr_t addr);

	bool reset(); // true if the reset was successful
	void decomission() { m_decomissioned = true; }
	bool is_decomissioned() const noexcept { return m_decomissioned; }

	Script(const std::vector<uint8_t>&, const vrt_ctx*, const vmod_riscv_machine*, const MachineInstance&);
	Script(const Script& source, const vrt_ctx*, const vmod_riscv_machine*);
	~Script();

private:
	void setup_virtual_memory(bool init);
	void handle_exception(gaddr_t);
	void handle_timeout(gaddr_t);
	bool install_binary(const std::string& file, bool shared = true);
	bool machine_initialize(bool init);
	void machine_setup(machine_t&, bool init);
	void setup_syscall_interface(machine_t&);

	machine_t m_machine;
	const vrt_ctx* m_ctx;
	const struct vmod_riscv_machine* m_vrm = nullptr;
	const MachineInstance& m_inst;
	void*       m_arena = nullptr;
	gaddr_t     m_shm_address = RO_AREA_END; /* It's a stack */

	std::string m_want_result;
	int         m_want_status = 403;
	bool        m_decomissioned = false;

	struct RegexCache {
		struct vre* re   = nullptr;
		uint32_t    hash = 0;
		bool        non_owned = false;
	};
	eastl::fixed_vector<RegexCache, 16> m_regex_cache;
};

template <typename... Args>
inline long Script::call(gaddr_t address, Args&&... args)
{
	try {
		// reset the stack pointer to an initial location (deliberately)
		machine().cpu.reset_stack_pointer();
		// setup calling convention
		machine().setup_call(address, std::forward<Args>(args)...);
		// execute function
		machine().simulate<true>(max_instructions());
		// address-sized integer return value
		return machine().cpu.reg(riscv::RISCV::REG_ARG0);
	}
	catch (const riscv::MachineTimeoutException& e) {
		this->handle_timeout(address);
	}
	catch (const std::exception& e) {
		this->handle_exception(address);
	}
	return -1;
}

template <typename... Args>
inline long Script::preempt(gaddr_t address, Args&&... args)
{
	const auto regs = machine().cpu.registers();
	try {
		const long ret = machine().preempt<50'000, true, false>(
			address, std::forward<Args>(args)...);
		machine().cpu.registers() = regs;
		return ret;
	}
	catch (const riscv::MachineTimeoutException& e) {
		this->handle_timeout(address);
	}
	catch (const std::exception& e) {
		this->handle_exception(address);
	}
	machine().cpu.registers() = regs;
	return -1;
}
