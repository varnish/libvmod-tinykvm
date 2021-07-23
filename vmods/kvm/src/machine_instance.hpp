#pragma once
#include <cassert>
#include <functional>
#include <tinykvm/machine.hpp>
#include "instance_cache.hpp"

struct vrt_ctx;
struct VSHA256Context;
struct vre;
struct director;

namespace kvm {

struct TenantInstance;
struct ProgramInstance;

class MachineInstance {
public:
	using gaddr_t = uint64_t;
	using machine_t = tinykvm::Machine;
	static constexpr size_t  REGEX_MAX    = 64;
	static constexpr size_t  DIRECTOR_MAX = 32;
	static constexpr size_t  RESULTS_MAX  = 3;

	// call any function, with any primitive arguments
	template <typename... Args>
	inline void call(gaddr_t addr, Args&&...);
	template <typename... Args>
	inline void debugcall(gaddr_t addr, Args&&...);

	void dynamic_call(uint32_t hash);

	auto& regex() { return m_regex; }
	auto& directors() { return m_directors; }

	auto& machine() { return m_machine; }
	const auto& machine() const { return m_machine; }

	const auto* ctx() const noexcept { return m_ctx; }
	void set_ctx(const vrt_ctx* ctx) { m_ctx = ctx; }
	const auto& tenant() const noexcept { return *m_tenant; }
	auto& instance() { return m_inst; }
	const auto& instance() const { return m_inst; }
	void assign_instance(std::shared_ptr<ProgramInstance>& ref) { m_inst_ref = std::move(ref); }

	uint64_t max_time() const noexcept;
	const std::string& name() const noexcept;
	const std::string& group() const noexcept;

	bool is_paused() const noexcept { return m_is_paused; }
	bool is_storage() const noexcept { return m_is_storage; }
	bool is_debug() const noexcept { return m_is_debug; }
	bool is_forkable() const noexcept { return machine().is_forkable(); }
	gaddr_t max_memory() const noexcept;

	void init_sha256();
	void hash_buffer(const char* buffer, int len);
	bool apply_hash();

	std::string symbol_name(gaddr_t address) const;
	gaddr_t resolve_address(const char* name) const { return machine().address_of(name); }

	void set_sigaction(int sig, gaddr_t handler);
	void print_backtrace(const gaddr_t addr);
	void open_debugger(uint16_t);

	MachineInstance(const std::vector<uint8_t>&, const vrt_ctx*, const TenantInstance*, ProgramInstance&, bool sto, bool dbg);
	MachineInstance(const MachineInstance& source, const vrt_ctx*, const TenantInstance*, ProgramInstance&);
	~MachineInstance();
	bool reset(); // true if the reset was successful

private:
	static void kvm_initialize();
	static void setup_syscall_interface();
	void handle_exception(gaddr_t);
	void handle_timeout(gaddr_t);

	machine_t m_machine;
	const vrt_ctx* m_ctx;
	const struct TenantInstance* m_tenant = nullptr;
	ProgramInstance& m_inst;

	bool        m_is_paused = false;
	bool        m_is_storage = false;
	bool        m_is_debug = false;
	bool        m_currently_debugging = false;
	gaddr_t     m_sighandler = 0;
	VSHA256Context* m_sha_ctx = nullptr;

	Cache<vre> m_regex;
	Cache<const director> m_directors;

	/* Deref this last */
	std::shared_ptr<ProgramInstance> m_inst_ref = nullptr;
};

template <typename... Args>
inline void MachineInstance::call(gaddr_t address, Args&&... args)
{
	try {
		machine().vmcall(address, std::forward<Args>(args)...);
	}
	catch (...) {
		this->handle_exception(address);
	}
}

} // kvm
