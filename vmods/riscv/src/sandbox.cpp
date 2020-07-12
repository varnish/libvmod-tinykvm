#include "script.hpp"

extern "C" {
# include "vdef.h"
# include "vrt.h"
	void *WS_Alloc(struct ws *ws, unsigned bytes);
}
inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
static std::vector<uint8_t> load_file(const std::string& filename);

struct vmod_riscv_machine {
	vmod_riscv_machine(std::vector<uint8_t> elf, VRT_CTX,
		uint64_t insn, uint64_t mem, uint64_t heap)
		: binary{std::move(elf)}, script{binary, ctx, insn, mem, heap},
		  max_instructions(insn), max_memory(mem), max_heap(heap) {}

	const std::vector<uint8_t> binary;
	Script   script;
	uint64_t max_instructions;
	uint64_t max_memory;
	uint64_t max_heap;
};

extern "C"
vmod_riscv_machine* riscv_create(const char* file, VRT_CTX, uint64_t insn)
{
	auto* vrm = (vmod_riscv_machine*) WS_Alloc(ctx->ws, sizeof(vmod_riscv_machine));
	return new (vrm) vmod_riscv_machine(load_file(file), ctx,
		/* Max instr: */ insn, /* Mem: */ 8*1024*1024, /* Heap: */ 6*1024*1024);
}

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	/* Allocate Script on workspace, and construct it in-place */
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t0);
#endif
	auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
	try {
		new (script) Script{vrm->script, ctx,
			vrm->max_instructions, vrm->max_memory, vrm->max_heap};

	} catch (std::exception& e) {
		printf(">>> Exception: %s\n", e.what());
		return -1;
	}
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t1);
	printf("Time spent in initialization: %ld ns\n", nanodiff(t0, t1));
#endif

	/* Call into the virtual machine */
	int ret = script->call(func);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
	printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
#endif

	script->~Script(); /* call destructor */
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t3);
	printf("Time spent in destructor: %ld ns\n", nanodiff(t2, t3));
	printf("Time spent total: %ld ns\n", nanodiff(t0, t3));
#endif

	return ret;
}

extern "C"
int riscv_free(vmod_riscv_machine* vrm)
{
	vrm->~vmod_riscv_machine();
	return 0;
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
	return (end_time.tv_sec - start_time.tv_sec) * (long)1e9 + (end_time.tv_nsec - start_time.tv_nsec);
}
