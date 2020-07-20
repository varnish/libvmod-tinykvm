#include "script.hpp"
#include "varnish.hpp"

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
static std::vector<uint8_t> load_file(const std::string& filename);
// functions used by all machines created during init, afterwards
static std::vector<const char*> lookup_wishlist;
static const size_t TOO_SMALL = 8;

struct vmod_riscv_machine {
	vmod_riscv_machine(const char* name, std::vector<uint8_t> elf,
		VRT_CTX, uint64_t insn, uint64_t mem, uint64_t heap)
		: binary{std::move(elf)}, script{binary, ctx, name, insn, mem, heap}
	{
		for (const auto* func : lookup_wishlist) {
			/* NOTE: We can't check if addr is 0 here, because
			   the wishlist applies to ALL machines. */
			const auto addr = lookup(func);
			sym_lookup.emplace(strdup(func), addr);
			const auto callsite = script.callsite(addr);
			sym_vector.push_back({func, addr, callsite.size});
		}
	}

	inline uint32_t lookup(const char* name) const {
		const auto& it = sym_lookup.find(name);
		if (it != sym_lookup.end()) return it->second;
		// fallback
		return script.resolve_address(name);
	}
	inline auto callsite(const char* name) {
		auto addr = lookup(name);
		return script.callsite(addr);
	}

	const uint64_t magic = 0xb385716f486938e6;
	const std::vector<uint8_t> binary;
	Script   script;
	/* Lookup tree for ELF symbol names */
	eastl::string_map<uint32_t,
			eastl::str_less<const char*>,
			eastl::allocator_malloc> sym_lookup;
	/* Index vector for ELF symbol names, used by call_index(..) */
	struct Lookup {
		const char* func;
		uint32_t    addr;
		size_t      size;
	};
	std::vector<Lookup> sym_vector;
};
#define SCRIPT_MAGIC 0x83e59fa5

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");


extern "C"
vmod_riscv_machine* riscv_create(const char* name,
	const char* file, VRT_CTX, uint64_t insn)
{
	auto* vrm = (vmod_riscv_machine*) WS_Alloc(ctx->ws, sizeof(vmod_riscv_machine));
	if (UNLIKELY(vrm == nullptr))
		VRT_fail(ctx, "Out of workspace");

	new (vrm) vmod_riscv_machine(name, load_file(file), ctx,
		/* Max instr: */ insn, /* Mem: */ 8*1024*1024, /* Heap: */ 6*1024*1024);
	return vrm;
}

extern "C"
void riscv_prewarm(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	(void) ctx;
	const auto site = vrm->callsite(func);
	vrm->sym_lookup.emplace(strdup(site.name.c_str()), site.address);
	vrm->sym_vector.push_back({site.name.c_str(), site.address, site.size});
}

inline int forkcall(VRT_CTX, vmod_riscv_machine* vrm, uint32_t addr)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (!priv_task->priv)
	{
		/* Allocate Script on workspace, and construct it in-place */
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
		if (UNLIKELY(script == nullptr)) {
			VRT_fail(ctx, "Out of workspace");
			return -1;
		}

		try {
			new (script) Script{vrm->script, ctx, vrm};

		} catch (std::exception& e) {
			printf(">>> Exception: %s\n", e.what());
			return -1;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent in initialization: %ld ns\n", nanodiff(t0, t1));
	#endif

		priv_task->priv = script;
		priv_task->len  = SCRIPT_MAGIC;
		priv_task->free = [] (void* script) {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
		#endif
			((Script*) script)->~Script(); /* call destructor */
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t3);
			printf("Time spent in destructor: %ld ns\n", nanodiff(t2, t3));
		#endif
			};
	}
	auto* script = (Script*) priv_task->priv;

	/* Call into the virtual machine */
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
#endif
	int ret = script->call(addr);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t3);
	printf("Time spent in forkcall(): %ld ns\n", nanodiff(t2, t3));
#endif
	return ret;
}

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	const auto addr = vrm->lookup(func);
	if (UNLIKELY(addr == 0)) {
		VRT_fail(ctx, "VM call failed (no such function: '%s')", func);
		return -1;
	}
	int ret = forkcall(ctx, vrm, addr);
	if (UNLIKELY(ret < 0)) {
		VSLb(ctx->vsl, SLT_Error, "VM call '%s' failed. Return value: %d",
			func, ret);
		VRT_fail(ctx, "VM call failed (negative result: %d)", ret);
	}
	return ret;
}
extern "C"
int riscv_forkcall_idx(VRT_CTX, vmod_riscv_machine* vrm, int index)
{
	if (index >= 0 && index < vrm->sym_vector.size())
	{
		auto& entry = vrm->sym_vector[index];
		if (UNLIKELY(entry.addr == 0)) {
			VRT_fail(ctx, "VM call failed (function '%s' at index %d not available)",
				entry.func, index);
			return -1;
		}
		if (UNLIKELY(entry.size <= TOO_SMALL)) {
			VSLb(ctx->vsl, SLT_Debug, "VM call '%s' is being skipped.",
				entry.func);
			//printf("Callsite: %s -> %zu\n", entry.func, entry.size);
			return 0;
		}

		int ret = forkcall(ctx, vrm, entry.addr);
		if (UNLIKELY(ret < 0)) {
			VSLb(ctx->vsl, SLT_Error,
				"VM call '%s' failed. Return value: %d", entry.func, ret);
			VRT_fail(ctx, "VM call failed (negative result)");
		}
		return ret;
	}
	VRT_fail(ctx, "VM call failed (invalid call index given: %d)", index);
	return -1;
}

extern "C"
int riscv_free(vmod_riscv_machine* vrm)
{
	vrm->~vmod_riscv_machine();
	return 0;
}

extern "C"
void riscv_add_known(VRT_CTX, const char* func)
{
	(void) ctx;
	lookup_wishlist.push_back(func);
}

inline Script* get_machine(VRT_CTX)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (priv_task->priv && priv_task->len == SCRIPT_MAGIC)
		return (Script*) priv_task->priv;
	return nullptr;
}

extern "C"
int riscv_current_call(VRT_CTX, const char* func)
{
	auto* script = get_machine(ctx);
	if (script) {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		const auto addr = script->vrm()->lookup(func);
		if (UNLIKELY(addr == 0)) {
			VRT_fail(ctx, "VM call failed (no such function: '%s')", func);
			return -1;
		}
		int ret = script->call(addr);
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return ret;
	}
	return -1;
}
extern "C"
int riscv_current_call_idx(VRT_CTX, int index)
{
	auto* script = get_machine(ctx);
	if (script) {
		if (index >= 0 && index < script->vrm()->sym_vector.size())
		{
			auto& entry = script->vrm()->sym_vector[index];
			if (UNLIKELY(entry.addr == 0)) {
				VRT_fail(ctx, "VM call failed (function '%s' at index %d not available)",
					entry.func, index);
				return -1;
			}
			if (UNLIKELY(entry.size <= TOO_SMALL)) {
				VSLb(ctx->vsl, SLT_Debug, "VM call '%s' is being skipped.",
					entry.func);
				//printf("Callsite: %s -> %zu\n", entry.func, entry.size);
				return 0;
			}
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t1);
		#endif
			int ret = script->call(entry.addr);
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return ret;
		}
		VRT_fail(ctx, "VM call failed (invalid index given: %d)", index);
		return -1;
	}
	VRT_fail(ctx, "VM call failed (no running machine)");
	return -1;
}

extern "C"
const char* riscv_current_name(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->name();
	return nullptr;
}
extern "C"
const char* riscv_current_result(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->want_result();
	return nullptr;
}
extern "C"
int riscv_current_result_status(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->want_status();
	return 400;
}

#include <unistd.h>
std::vector<uint8_t> load_file(const std::string& filename)
{
    size_t size = 0;
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) throw std::runtime_error("Could not open file: " + filename);

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> result(size);
    if (size != fread(result.data(), 1, size, f))
    {
        fclose(f);
        throw std::runtime_error("Error when reading from file: " + filename);
    }
    fclose(f);
    return result;
}

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
