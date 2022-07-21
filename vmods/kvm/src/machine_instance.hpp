#pragma once
#include <cassert>
#include <tinykvm/machine.hpp>
#include "instance_cache.hpp"

struct vrt_ctx;
struct VSHA256Context;
struct vre;
struct director;

namespace kvm {
class TenantInstance;
class ProgramInstance;

/**
 * MachineInstance is a collection of state that is per VM,
 * and per request. It will keep things like file descriptors,
 * backends, regex handles and such. And most importanatly,
 * it holds an actual KVM VM that is based on the tenants program.
 *
 * Once the request ends and this instance dies, it will decrease
 * refcounts on a few things, so if the tenant sends a new program,
 * the old program is kept alive until all requests that are using
 * it ends.
**/
class MachineInstance {
public:
	using gaddr_t = uint64_t;
	using machine_t = tinykvm::Machine;
	static constexpr size_t  REGEX_MAX    = 64;
	static constexpr size_t  DIRECTOR_MAX = 32;
	static constexpr size_t  RESULTS_MAX  = 3;

	void print(std::string_view text);
	void dynamic_call(uint32_t hash);

	auto& regex() { return m_regex; }
	auto& directors() { return m_directors; }

	auto& machine() { return m_machine; }
	const auto& machine() const { return m_machine; }

	void copy_to(uint64_t addr, const void*, size_t, bool zeroes = false);

	const auto* ctx() const noexcept { return m_ctx; }
	void set_ctx(const vrt_ctx* ctx) { m_ctx = ctx; }
	const auto& tenant() const noexcept { return *m_tenant; }
	auto& instance() { return *m_inst; }
	const auto& instance() const { return *m_inst; }

	uint64_t max_time() const noexcept;
	const std::string& name() const noexcept;
	const std::string& group() const noexcept;

	bool is_debug() const noexcept { return m_is_debug; }
	bool is_storage() const noexcept { return m_is_storage; }
 	gaddr_t shared_memory_boundary() const noexcept;
	gaddr_t shared_memory_size() const noexcept;
	void set_global_memory_shared(bool v) noexcept { m_global_shared_memory = v; }

	void wait_for_requests() { m_waiting_for_requests = true; }
	bool is_waiting_for_requests() const noexcept { return m_waiting_for_requests; }
	/* With this we can enforce that certain syscalls have been invoked before
	   we even check the validity of responses. This makes sure that crashes does
	   not accidentally produce valid responses, which can cause confusion. */
	void begin_backend_call() { m_backend_response_called = false; }
	void finish_backend_call() { m_backend_response_called = true; }
	bool backend_response_called() const noexcept { return m_backend_response_called; }

	void init_sha256();
	void hash_buffer(const char* buffer, int len);
	bool apply_hash();

	std::string symbol_name(gaddr_t address) const;
	gaddr_t resolve_address(const char* name) const { return machine().address_of(name); }

	void set_sigaction(int sig, gaddr_t handler);
	void print_backtrace(const gaddr_t addr);
	void open_debugger(uint16_t);

	static void kvm_initialize();
	MachineInstance(const std::vector<uint8_t>&, const vrt_ctx*, const TenantInstance*, ProgramInstance*, bool dbg);
	MachineInstance(const MachineInstance&, const vrt_ctx*, const TenantInstance*, ProgramInstance*);
	void initialize();
	~MachineInstance();
	void tail_reset();
	void reset_to(const vrt_ctx*, MachineInstance&);

private:
	static void setup_syscall_interface();
	void handle_exception(gaddr_t);
	void handle_timeout(gaddr_t);
	void sanitize_path(char*, size_t);
	void sanitize_file(char*, size_t);
	tinykvm::Machine::printer_func get_vsl_printer();

	const vrt_ctx* m_ctx;
	machine_t m_machine;
	const TenantInstance* m_tenant = nullptr;
	ProgramInstance* m_inst;
	bool        m_is_debug = false;
	bool        m_is_storage = false;
	bool        m_waiting_for_requests = false;
	bool        m_backend_response_called = false;
	bool        m_global_shared_memory = false;
	bool        m_last_newline = true;
	gaddr_t     m_sighandler = 0;
	VSHA256Context* m_sha_ctx = nullptr;

	Cache<int> m_fd;
	Cache<vre*> m_regex;
	Cache<const director*> m_directors;
};

} // kvm
