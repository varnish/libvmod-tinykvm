#include "script.hpp"

#include <libriscv/native_heap.hpp>
#include "machine/include_api.hpp"
#include "sandbox.hpp"
#include "varnish.hpp"
extern "C" void riscv_SetHash(struct req*, VSHA256_CTX*);
inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
static constexpr uint64_t SIGHANDLER_INSN = 60'000;
static constexpr unsigned NATIVE_SYSCALLS_BASE = 80;
static constexpr bool VERBOSE_ERRORS = true;

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

Script::Script(
	const Script& source, const vrt_ctx* ctx,
	const SandboxTenant* vrm, MachineInstance& inst)
	: m_machine(source.machine(), {
		.memory_max = 0,
	  }),
	  m_ctx(ctx), m_tenant(vrm), m_inst(inst),
	  m_is_debug(source.is_debug()),
	  m_sighandler{source.m_sighandler},
	  m_regex     {vrm->config.max_regex()},
	  m_directors {vrm->config.max_backends()}
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	/* No initialization */
	this->machine_setup(machine(), false);

	/* Transfer allocations from the source machine, to fully replicate heap */
	source.machine().arena().transfer(machine().arena());
	/* Load the compiled regexes of the source */
	m_regex.loan_from(source.m_regex);
	/* Load the directors of the source */
	m_directors.loan_from(source.m_directors);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Total time in Script constr body: %ldns\n", nanodiff(t0, t1));
#endif
}

Script::Script(
	const std::vector<uint8_t>& binary, const vrt_ctx* ctx,
	const SandboxTenant* vrm, MachineInstance& inst,
	bool storage, bool debug)
	: m_machine(binary, {
		.memory_max = vrm->config.max_memory(),
#ifdef RISCV_BINARY_TRANSLATION
		// Time-saving translator options
		// NOTE: 0 means translator is disabled (for debugging)
		.translate_blocks_max = (debug ? 0u : 4000u),
		.forward_jumps = false,
#endif
	  }),
	  m_ctx(ctx), m_tenant(vrm), m_inst(inst),
	  m_is_storage(storage), m_is_debug(debug),
	  m_regex     {vrm->config.max_regex()},
	  m_directors {vrm->config.max_backends()}
{
	this->machine_initialize();
}

void Script::init()
{
	setup_syscall_interface();
}

Script::~Script()
{
	// free any owned regex pointers
	m_regex.foreach_owned(
		[] (auto& entry) {
			VRE_free(&entry.item);
		});
	if (this->is_debug()) {
		this->stop_debugger();
	}
}

void Script::setup_virtual_memory(bool /*init*/)
{
	using namespace riscv;
	auto& mem = machine().memory;
	// Use a different arena and stack for the storage machine
	this->m_heap_base = is_storage() ? SHEAP_BASE : CHEAP_BASE;

	mem.set_stack_initial(stack_begin());
	// this separates heap and stack
	mem.install_shared_page(
		stack_begin() / Page::size(), Page::guard_page());
}

void Script::machine_initialize()
{
	// setup system calls and traps
	this->machine_setup(machine(), true);
	// run through the initialization
	try {
		machine().simulate<true>(max_instructions());
	} catch (const riscv::MachineTimeoutException& e) {
		handle_timeout(machine().cpu.pc());
		throw;
	} catch (const std::exception& e) {
		handle_exception(machine().cpu.pc());
		throw;
	}
}
void Script::machine_setup(machine_t& machine, bool init)
{
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	machine.set_userdata<Script>(this);
	machine.set_printer(
		[this] (auto* data, size_t len) {
			/* TODO: Use VSLb here or disable this completely */
			printf(">>> %s: %.*s\n", this->name().c_str(), (int) len, data);
		});
	machine.set_stdin([] (auto*, size_t) { return 0; });

	if (init == false)
	{
		machine.memory.set_page_fault_handler(
		[this] (auto& mem, size_t pageno, bool init) -> riscv::Page& {
			/* Pages are allocated from workspace */
			auto* data =
				(riscv::PageData*) WS_Alloc(m_ctx->ws, riscv::Page::size());
			if (LIKELY(data != nullptr)) {
				if (init) {
					new (data) riscv::PageData();
				}
				return mem.allocate_page(pageno,
					riscv::PageAttributes{
						.non_owning = true, // don't delete!
						.user_defined = 1 },
					data);
			}
			// With heap as fallback, to allow bigger VMs
			const auto mem_usage = mem.owned_pages_active() * riscv::Page::size();
			if (mem_usage < this->tenant().config.max_memory()) {
				using namespace riscv;
				return mem.allocate_page(pageno, init ? PageData::INITIALIZED : PageData::UNINITIALIZED);
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory (max_memory limit reached)",
					pageno * riscv::Page::size());
		});
		machine.memory.set_page_write_handler(
		[this] (auto& mem, gaddr_t pageno, riscv::Page& page) -> void {
			assert(page.has_data() && page.attr.is_cow);
			/* Pages are allocated from workspace */
			auto* data =
				(riscv::PageData*) WS_Copy(m_ctx->ws, page.data(), page.size());
			if (LIKELY(data != nullptr)) {
				/* Release any already non-owned data */
				if (page.attr.non_owning)
					page.m_page.release();
				page.attr.write = true;
				page.attr.is_cow = false;
				page.attr.non_owning = true; /* Avoid calling delete */
				page.m_page.reset(data);
				return;
			}
			// With heap as fallback, to allow bigger VMs
			const auto mem_usage = mem.owned_pages_active() * riscv::Page::size();
			if (mem_usage < this->tenant().config.max_memory()) {
				page.make_writable();
				return;
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory (max_memory limit reached)",
					pageno * riscv::Page::size());
		});
	}
	else {
		machine.memory.set_page_fault_handler(
			[this] (auto& mem, size_t pageno, bool) -> riscv::Page& {
				bool dont_fork = false;
				const size_t stack_pbase = stack_base() / riscv::Page::size();
				const size_t stack_pend  = stack_begin() / riscv::Page::size();
				if (pageno >= stack_pbase && pageno < stack_pend) {
					dont_fork = true;
				}
				auto* data = new riscv::PageData {};
				return mem.allocate_page(pageno,
					riscv::PageAttributes{
						.dont_fork  = dont_fork,
						.user_defined = 1 },
					data);
			});
	}

#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
#endif
	// page protections and "hidden" stacks
	this->setup_virtual_memory(init);

#ifdef RISCV_DEBUG
	machine.verbose_instructions = true;
	//machine.verbose_registers = true;
#endif

	if (init)
	{
		// Full Linux-compatible stack
		machine.cpu.reset_stack_pointer(); // DONT TOUCH (YES YOU)
		machine.setup_linux(
			{ name(), m_is_storage ? "1" : "0", is_debug() ? "1" : "0" },
			{ "LC_CTYPE=C", "LC_ALL=C", "USER=groot" });
	}

	// add system call interface
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
#endif
	machine.setup_native_heap(NATIVE_SYSCALLS_BASE,
		arena_base(), vrm()->config.max_heap());

#ifdef ENABLE_TIMING
	TIMING_LOCATION(t3);
#endif
	machine.setup_native_memory(NATIVE_SYSCALLS_BASE+5);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t4);
#endif

#ifdef ENABLE_TIMING
	TIMING_LOCATION(t5);
	printf("[Constr] pagetbl: %ldns, vmem: %ldns, arena: %ldns, nat.mem: %ldns, syscalls: %ldns\n",
		nanodiff(t0, t1), nanodiff(t1, t2), nanodiff(t2, t3), nanodiff(t3, t4), nanodiff(t4, t5));
#endif
}

void Script::handle_exception(gaddr_t address)
{
	try {
		throw;
	}
	catch (const riscv::MachineException& e) {
		if constexpr (VERBOSE_ERRORS) {
		fprintf(stderr, "Script exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
		fprintf(stderr, ">>> Machine registers:\n[PC\t%08lX] %s\n",
			(long) machine().cpu.pc(),
			machine().cpu.registers().to_string().c_str());
		}
		VRT_fail(m_ctx, "Script exception: %s", e.what());
	}
	catch (const std::exception& e) {
		if constexpr (VERBOSE_ERRORS) {
			fprintf(stderr, "Script exception: %s\n", e.what());
		}
		VRT_fail(m_ctx, "Script exception: %s", e.what());
	}
	if constexpr (VERBOSE_ERRORS) {
		printf("Program page: %s\n", machine().memory.get_page_info(machine().cpu.pc()).c_str());
		printf("Stack page: %s\n", machine().memory.get_page_info(machine().cpu.reg(2)).c_str());

		auto callsite = machine().memory.lookup(address);
		fprintf(stderr, "Function call: %s\n", callsite.name.c_str());
		this->print_backtrace(address);
	}
	if (this->m_sighandler != 0) {
		auto handler = this->m_sighandler;
		//machine().stack_push(machine().cpu.pc());
		machine().stack_push(machine().cpu.reg(riscv::REG_RA));
		machine().cpu.reg(riscv::REG_RA) = machine().cpu.pc();
		machine().cpu.reg(riscv::REG_ARG0) = 11; /* SIGSEGV */
		machine().cpu.jump(handler);
		this->m_sighandler = 0;
		this->resume(SIGHANDLER_INSN);
		this->m_sighandler = handler;
	}
}
void Script::handle_timeout(gaddr_t address)
{
	if constexpr (VERBOSE_ERRORS) {
		auto callsite = machine().memory.lookup(address);
		fprintf(stderr, "Script hit max instructions (%zu) for: %s\n",
			max_instructions(), callsite.name.c_str());
	}
	VRT_fail(m_ctx, "Script for '%s' timed out", name().c_str());
}
void Script::print_backtrace(const gaddr_t addr)
{
	machine().memory.print_backtrace(
		[] (std::string_view line) {
			printf("-> %.*s\n", (int)line.size(), line.begin());
		});
	auto origin = machine().memory.lookup(addr);
	printf("-> [-] 0x%08lx + 0x%.3x: %s\n",
			(long) origin.address,
			origin.offset, origin.name.c_str());
}
void Script::set_sigaction(int, gaddr_t handler)
{
	this->m_sighandler = handler;
}

uint64_t Script::max_instructions() const noexcept {
	return vrm()->config.max_instructions();
}
const std::string& Script::name() const noexcept {
	return vrm()->config.name;
}
const std::string& Script::group() const noexcept {
	return vrm()->config.group.name;
}
Script::gaddr_t Script::max_memory() const noexcept {
	return vrm()->config.max_memory();
}
Script::gaddr_t Script::stack_base() const noexcept {
	return stack_begin() - stack_size();
}
size_t  Script::stack_size() const noexcept {
	return 0x100000;
}
size_t Script::heap_size() const noexcept {
	return vrm()->config.max_heap();
}

Script::gaddr_t Script::guest_alloc(size_t len)
{
	return machine().arena().malloc(len);
}
bool Script::guest_free(gaddr_t addr)
{
	return machine().arena().free(addr) == 0;
}


inline void Script::init_sha256()
{
	if (ctx()->req == nullptr) {
		throw std::runtime_error("SHA256 not available during initialization");
	}
	this->m_sha_ctx =
		(VSHA256_CTX*) WS_Alloc(ctx()->ws, sizeof(VSHA256_CTX));
	if (!this->m_sha_ctx)
		throw std::runtime_error("SHA256: Out of workspace");
	VSHA256_Init(this->m_sha_ctx);
}
void Script::hash_buffer(const char* buffer, int len)
{
	if (ctx() == nullptr) return;
	if (m_sha_ctx == nullptr) init_sha256();

	VSHA256_Update(this->m_sha_ctx, buffer, len);
	VSLb(ctx()->vsl, SLT_Hash, "%.*s", len, buffer);
}
bool Script::apply_hash()
{
	if (m_sha_ctx && ctx()->req) {
		riscv_SetHash(ctx()->req, m_sha_ctx);
		return true;
	}
	return false;
}

Script::gaddr_t Script::resolve_address(const char* name) const {
	return machine().address_of(name);
}
std::string Script::symbol_name(gaddr_t address) const
{
	auto callsite = machine().memory.lookup(address);
	return callsite.name;
}

void Script::dynamic_call(uint32_t hash)
{
	vrm()->dynamic_call(hash, *this);
}

#ifdef ENABLE_TIMING
timespec time_now()
{
	timespec t;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t;
}
long nanodiff(timespec start_time, timespec end_time)
{
	assert(end_time.tv_sec == 0); /* We should never use seconds */
	return end_time.tv_nsec - start_time.tv_nsec;
}
#endif
