#pragma once
#include <cassert>
#include <functional>
#include <libriscv/machine.hpp>
#include <EASTL/fixed_vector.h>
struct vrt_ctx;

class Script {
public:
	static constexpr uint32_t HIDDEN_AREA = 0x10000;

	// call any script function, with any parameters
	template <typename... Args>
	inline long call(const std::string& name, Args&&...);

	template <typename... Args>
	inline long call(uint32_t addr, Args&&...);

	template <typename... Args>
	inline long preempt(uint32_t addr, Args&&...);

	auto& machine() { return m_machine; }
	const auto& machine() const { return m_machine; }

	void set_ctx(const vrt_ctx* ctx) { m_ctx = ctx; }
	const auto* ctx() const noexcept { return m_ctx; }

	auto& shm_address() { return m_shm_address; }

	int    regex_find(uint32_t hash) const;
	size_t regex_manage(struct vre*, uint32_t hash);
	void   regex_free(size_t);
	struct vre* regex_get(size_t idx) { return m_regex_cache.at(idx).re; }

	std::string symbol_name(uint32_t address) const;
	uint32_t resolve_address(const std::string& name) const;

	void print_backtrace(const uint32_t addr);

	static auto& hidden_area() noexcept { return g_hidden_stack; }
	bool crashed() const noexcept { return m_crashed; }
	bool reset(); // true if the reset was successful

	Script(const riscv::Machine<riscv::RISCV32>&, const vrt_ctx*,
		uint64_t insn, uint64_t mem, uint64_t heap);
	~Script();
	static void init();

private:
	void setup_virtual_memory();
	void handle_exception(uint32_t);
	void handle_timeout(uint32_t);
	bool install_binary(const std::string& file, bool shared = true);
	bool machine_initialize();
	void machine_setup(riscv::Machine<riscv::RISCV32>&);
	void setup_syscall_interface(riscv::Machine<riscv::RISCV32>&);
	static riscv::Page g_hidden_stack; // page used by the internal APIs

	riscv::Machine<riscv::RISCV32> m_machine;
	const vrt_ctx* m_ctx = nullptr;
	uint64_t    m_max_instructions = 0;
	uint64_t    m_max_heap = 0;
	uint64_t    m_max_memory = 0;
	bool        m_crashed = false;
	int         m_budget_overruns = 0;

	uint32_t    m_shm_address = 0x10000;
	struct RegexCache {
		struct vre* re   = nullptr;
		uint32_t    hash = 0;
	};
	eastl::fixed_vector<RegexCache, 16> m_regex_cache;
};

template <typename... Args>
inline long Script::call(uint32_t address, Args&&... args)
{
	try {
		// reset the stack pointer to an initial location (deliberately)
		machine().cpu.reset_stack_pointer();
		// setup calling convention
		machine().setup_call(address, std::forward<Args>(args)...);
		// execute function
		machine().simulate<true>(m_max_instructions);
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
inline long Script::call(const std::string& func, Args&&... args)
{
	const auto address = machine().address_of(func.c_str());
	if (UNLIKELY(address == 0)) {
		fprintf(stderr, "Script::call could not find: %s!\n", func.c_str());
		return -1;
	}
	return call(address, std::forward<Args>(args)...);
}

template <typename... Args>
inline long Script::preempt(uint32_t address, Args&&... args)
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
