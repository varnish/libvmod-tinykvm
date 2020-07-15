#include "script.hpp"
#include "varnish.hpp"

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
static std::vector<uint8_t> load_file(const std::string& filename);

struct vmod_riscv_machine {
	vmod_riscv_machine(const char* name, std::vector<uint8_t> elf,
		VRT_CTX, uint64_t insn, uint64_t mem, uint64_t heap)
		: binary{std::move(elf)}, script{binary, ctx, name, insn, mem, heap} {}

	uint64_t magic = 0xb385716f486938e6;
	const std::vector<uint8_t> binary;
	Script   script;
};
#define SCRIPT_MAGIC 0x83e59fa5

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

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

inline int forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
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
			new (script) Script{vrm->script, ctx};

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

#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
#endif
	/* Call into the virtual machine */
	int ret = script->call(func);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
	printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
#endif
	return ret;
}

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	int ret = forkcall(ctx, vrm, func);
	if (UNLIKELY(ret < 0)) {
		VSLb(ctx->vsl, SLT_Error, "VM call '%s' failed. Return value: %d",
			func, ret);
		VRT_fail(ctx, "VM call failed (negative result)");
	}
	return ret;
}

extern "C"
int riscv_free(vmod_riscv_machine* vrm)
{
	vrm->~vmod_riscv_machine();
	return 0;
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
	if (script)
		return script->call(func);
	return -1;
}

extern "C"
const char* riscv_current_name(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->name().c_str();
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
