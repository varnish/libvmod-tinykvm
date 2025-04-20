#pragma once
#include <cassert>
#include <cstdarg>
#include <tinykvm/machine.hpp>
#include "instance_cache.hpp"
#include "machine_stats.hpp"

struct vrt_ctx;
struct vre;

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
	static constexpr size_t REGEX_MAX = 64;

	void print(std::string_view text) const;
	void logprint(std::string_view text, bool says = false) const;
	void logf(const char*, ...) const;

	auto& regex() { return m_regex; }

	auto& machine() { return m_machine; }
	const auto& machine() const { return m_machine; }

	void copy_to(uint64_t addr, const void*, size_t, bool zeroes = false);

	const vrt_ctx* ctx() const;
	bool has_ctx() const noexcept { return m_ctx != nullptr; }
	void set_ctx(const vrt_ctx* ctx) { m_ctx = ctx; }
	const auto& tenant() const noexcept { return *m_tenant; }
	auto& program() noexcept { return *m_inst; }
	const auto& program() const noexcept { return *m_inst; }

	float max_req_time() const noexcept;
	const std::string& name() const noexcept;
	const std::string& group() const noexcept;

	auto& stats() noexcept { return this->m_stats; }
	const auto& stats() const noexcept { return this->m_stats; }

	bool allows_debugging() const noexcept;
	bool is_debug() const noexcept { return m_is_debug; }
	bool is_storage() const noexcept { return m_is_storage; }
	bool is_ephemeral() const noexcept { return m_is_ephemeral; }
	gaddr_t shared_memory_boundary() const noexcept;
	gaddr_t shared_memory_size() const noexcept;
	void set_ephemeral(bool e) noexcept { m_is_ephemeral = e; }

	void reset_wait_for_requests() { m_waiting_for_requests = false; }
	void wait_for_requests() { m_waiting_for_requests = true; }
	/* For now, pausing does nothing. */
	void wait_for_requests_paused();
	bool is_waiting_for_requests() const noexcept { return m_waiting_for_requests; }
	/* With this we can enforce that certain syscalls have been invoked before
	   we even check the validity of responses. This makes sure that crashes does
	   not accidentally produce valid responses, which can cause confusion. */
	void begin_call() { m_response_called = 0; }
	void finish_call(uint8_t n) { m_response_called = n; }
	bool response_called(uint8_t n) const noexcept { return m_response_called == n; }
	void reset_needed_now() { m_reset_needed = true; }

	void init_sha256();
	void hash_buffer(const char* buffer, int len);
	bool apply_hash();

	std::string symbol_name(gaddr_t address) const;
	gaddr_t resolve_address(const char* name) const { return machine().address_of(name); }

	void set_sigaction(int sig, gaddr_t handler);
	void print_backtrace();
	void open_debugger(uint16_t, float timeout);
	void storage_debugger(float timeout);

	uint64_t allocate_post_data(size_t size);
	gaddr_t& get_inputs_allocation() { return m_inputs_allocation; }

	static void kvm_initialize();
	MachineInstance(const std::vector<uint8_t>&, const vrt_ctx*, const TenantInstance*, ProgramInstance*, bool storage, bool dbg);
	MachineInstance(unsigned reqid, const MachineInstance&, const TenantInstance*, ProgramInstance*);
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
	tinykvm::Machine::printer_func get_vsl_printer() const;

	const vrt_ctx* m_ctx;
	machine_t m_machine;
	const TenantInstance* m_tenant = nullptr;
	ProgramInstance* m_inst;
	const std::vector<uint8_t>& m_original_binary;
	bool        m_is_debug;
	const bool  m_is_storage;
	bool        m_is_ephemeral = true;
	bool        m_waiting_for_requests = false;
	uint8_t     m_response_called = 0;
	bool        m_reset_needed = false;
	bool        m_print_stdout = false;
	mutable bool m_last_newline = true;
	gaddr_t     m_sighandler = 0x0;

	gaddr_t     m_post_data = 0x0;
	size_t      m_post_size = 0;
	gaddr_t     m_inputs_allocation = 0x0;

	MachineStats m_stats;

	Cache<vre*> m_regex;
};

inline void MachineInstance::logf(const char *fmt, ...) const
{
	char buffer[2048];
	va_list va;
	va_start(va, fmt);
	/* NOTE: vsnprintf has an insane return value. */
	const int len = vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);
	if (len >= 0 && (size_t)len < sizeof(buffer)) {
		this->logprint(std::string_view{buffer, (size_t)len}, false);
	} else {
		throw std::runtime_error("Printf buffer exceeded");
	}
}

} // kvm
