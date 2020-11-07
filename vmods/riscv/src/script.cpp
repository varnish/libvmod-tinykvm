#include "script.hpp"

#include <include/syscall_helpers.hpp>
#include "machine/include_api.hpp"
#include "sandbox.hpp"
#include "varnish.hpp"
extern "C" void riscv_SetHash(struct req*, VSHA256_CTX*);
inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);

static const bool TRUSTED_CALLS = true;
static constexpr bool VERBOSE_ERRORS = true;
static constexpr int HEAP_PAGENO   = 0x40000000 >> riscv::Page::SHIFT;
static constexpr int STACK_PAGENO  = HEAP_PAGENO - 1;

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

Script::Script(
	const Script& source, const vrt_ctx* ctx,
	const vmod_riscv_machine* vrm, const MachineInstance& inst)
	: m_machine(source.machine().memory.binary(), {
		.memory_max = 0,
		.owning_machine = &source.machine()
	  }),
	  m_ctx(ctx), m_vrm(vrm), m_inst(inst)
{
	/* No initialization */
	this->machine_setup(machine(), false);

	/* Transfer data from the old arena, to fully replicate heap */
	arena_transfer((sas_alloc::Arena*) source.m_arena, (sas_alloc::Arena*) m_arena);
	/* Load the compiled regexes of the source */
	for (auto& regex : source.m_regex_cache) {
		m_regex_cache.push_back(regex);
		m_regex_cache.back().non_owned = true;
	}
}

Script::Script(
	const std::vector<uint8_t>& binary, const vrt_ctx* ctx,
	const vmod_riscv_machine* vrm, const MachineInstance& inst)
	: m_machine(binary, { .memory_max = vrm->config.max_memory }),
	  m_ctx(ctx), m_vrm(vrm), m_inst(inst)
{
	this->machine_initialize();
}

Script::~Script()
{
	// free any unfreed regex pointers
	for (auto& entry : m_regex_cache)
		if (entry.re && !entry.non_owned)
			VRE_free(&entry.re);
}

void Script::setup_virtual_memory(bool init)
{
	using namespace riscv;
	auto& mem = machine().memory;
	mem.set_stack_initial(STACK_PAGENO * Page::size());
	// this separates heap and stack
	mem.install_shared_page(STACK_PAGENO, Page::guard_page());
	// don't fork the shared memory areas - it will cause syscall trouble
	auto* ws = (init) ? nullptr : ctx()->ws;
	new (&ro_area) MemArea<MARCH> (machine(), RO_AREA_BEGIN, RO_AREA_END,
		{ .write = false, .non_owning = true, .dont_fork = true }, ws);
	mem.install_shared_page(RO_AREA_END >> 12, Page::guard_page());
	new (&rw_area) MemArea<MARCH> (machine(), RW_AREA_BEGIN, RW_AREA_END,
		{ .write = true, .non_owning = true, .dont_fork = true }, ws);
	mem.install_shared_page(RW_AREA_END >> 12, Page::guard_page());
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
	// catch program timeouts
	if (UNLIKELY(machine().instruction_counter() >= max_instructions())) {
		throw riscv::MachineTimeoutException(riscv::MAX_INSTRUCTIONS_REACHED,
			"Maximum instruction counter reached", max_instructions());
	}
}
void Script::machine_setup(machine_t& machine, bool init)
{
	machine.set_userdata<Script>(this);

	if (init == false)
	{
		machine.memory.set_page_fault_handler(
		[this] (auto& mem, size_t pageno) -> riscv::Page& {
			/* Pages are allocated from workspace */
			//printf("Creating page %zu @ 0x%X\n", pageno, pageno * 4096);
			auto* data =
				(riscv::PageData*) WS_Alloc(m_ctx->ws, riscv::Page::size());
			if (LIKELY(data != nullptr)) {
				return mem.allocate_page(pageno,
					riscv::PageAttributes{
						.non_owning = true, // don't delete!
						.user_defined = 1 },
					data);
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory", mem.pages_active());
		});
		machine.memory.set_page_write_handler(
		[this] (auto& mem, riscv::Page& page) -> void {
			assert(page.has_data() && page.attr.is_cow);
			/* Pages are allocated from workspace */
			auto* data =
				(riscv::PageData*) WS_Copy(m_ctx->ws, page.data(), page.size());
			if (LIKELY(data != nullptr)) {
				/* Release any already non-owned data */
				if (page.attr.non_owning)
					page.m_page.release();
				page.attr.is_cow = false;
				page.attr.non_owning = true; /* Avoid calling delete */
				page.m_page.reset(data);
				return;
			}
			throw riscv::MachineException(
				riscv::OUT_OF_MEMORY, "Out of memory", mem.pages_active());
		});
	}
	else {
		machine.memory.set_page_fault_handler(
			[] (auto& mem, size_t pageno) -> riscv::Page& {
				bool dont_fork = false;
				if (pageno >= STACK_PAGENO-128 && pageno < STACK_PAGENO) {
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

	// page protections and "hidden" stacks
	this->setup_virtual_memory(init);
	// stack
	machine.cpu.reset_stack_pointer();

	if (init)
	{
		const auto exit_addr = machine.address_of("exit");
		if (exit_addr)
			machine.memory.set_exit_address(exit_addr);
		else
			throw std::runtime_error("The binary is missing a public exit function!");
		// Full Linux-compatible stack
		machine.setup_linux(
			{ name() },
			{ "LC_CTYPE=C", "LC_ALL=C", "USER=groot" });
	}

	// add system call interface
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	if (init == false)
	{
		this->m_arena = setup_native_heap_syscalls<MARCH>(
			machine, vrm()->config.max_heap, [this] (size_t size) {
				return WS_Alloc(m_ctx->ws, size);
			});
	} else {
		this->m_arena =
			setup_native_heap_syscalls<MARCH>(machine, vrm()->config.max_heap);
	}

#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
#endif
	setup_native_memory_syscalls<MARCH>(machine, TRUSTED_CALLS);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
#endif
	setup_syscall_interface(machine);

	machine.on_unhandled_syscall(
		[] (int number) {
			//VSLb(m_ctx->vsl, SLT_Debug,
			//	"VM unhandled system call: %d\n", number);
			printf("VM unhandled system call: %d\n", number);
		});
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t3);
	printf("Time spent setting up arena: %ld ns, nat.mem: %ld ns, syscalls: %ld ns\n",
		nanodiff(t0, t1), nanodiff(t1, t2), nanodiff(t2, t3));
#endif
}

void Script::handle_exception(gaddr_t address)
{
	try {
		throw;
	}
	catch (const riscv::MachineException& e) {
		if constexpr (VERBOSE_ERRORS) {
		fprintf(stderr, "Script exception: %s (data: %#x)\n", e.what(), e.data());
		fprintf(stderr, ">>> Machine registers:\n[PC\t%08lX] %s\n",
			(long) machine().cpu.pc(),
			machine().cpu.registers().to_string().c_str());
		}
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
}
void Script::handle_timeout(gaddr_t address)
{
	if constexpr (VERBOSE_ERRORS) {
		auto callsite = machine().memory.lookup(address);
		fprintf(stderr, "Script hit max instructions for: %s\n",
			callsite.name.c_str());
	}
	VRT_fail(m_ctx, "Script for '%s' timed out", name().c_str());
}
void Script::print_backtrace(const gaddr_t addr)
{
	machine().memory.print_backtrace(
		[] (const char* buffer, size_t len) {
			printf("-> %.*s\n", (int)len, buffer);
		});
	auto origin = machine().memory.lookup(addr);
	printf("-> [-] 0x%08lx + 0x%.3x: %s\n",
			(long) origin.address,
			origin.offset, origin.name.c_str());
}

uint64_t Script::max_instructions() const noexcept
{
	return vrm()->config.max_instructions;
}
const std::string& Script::name() const noexcept
{
	return vrm()->config.name;
}
const std::string& Script::group() const noexcept
{
	return vrm()->config.group;
}

Script::gaddr_t Script::guest_alloc(size_t len)
{
	return arena_malloc((sas_alloc::Arena*) m_arena, len);
}

inline void Script::init_sha256()
{
	this->m_sha_ctx =
		(VSHA256_CTX*) WS_Alloc(ctx()->ws, sizeof(VSHA256_CTX));
	if (!this->m_sha_ctx)
		throw std::runtime_error("Out of workspace");
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
	if (m_sha_ctx && ctx()) {
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

int Script::regex_find(uint32_t hash) const
{
	for (unsigned idx = 0; idx < m_regex_cache.size(); idx++) {
		if (m_regex_cache[idx].hash == hash) return idx;
	}
	return -1;
}
size_t Script::regex_manage(struct vre* ptr, uint32_t hash)
{
	m_regex_cache.push_back({ptr, hash});
	return m_regex_cache.size() - 1;
}
void Script::regex_free(size_t idx)
{
	m_regex_cache.at(idx) = { nullptr, 0 };
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
