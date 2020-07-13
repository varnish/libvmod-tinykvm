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
	eastl::fixed_vector<uint32_t, 8> lookup;
	inline void lookup_add(const std::string& name) {
		lookup.push_back(script.resolve_address(name));
	}
};

extern "C"
vmod_riscv_machine* riscv_create(const char* file, VRT_CTX, uint64_t insn)
{
	auto* vrm = (vmod_riscv_machine*) WS_Alloc(ctx->ws, sizeof(vmod_riscv_machine));
	new (vrm) vmod_riscv_machine(load_file(file), ctx,
		/* Max instr: */ insn, /* Mem: */ 8*1024*1024, /* Heap: */ 6*1024*1024);
	vrm->lookup_add("on_client_request");
	vrm->lookup_add("on_synth");
	vrm->lookup_add("on_backend_response");
	return vrm;
}

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

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
		priv_task->priv = script;
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
	int ret = script->call(addr);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
	printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
#endif
	return ret;
}

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	return forkcall(ctx, vrm, vrm->script.resolve_address(func));
}
extern "C"
int riscv_forkcall_idx(VRT_CTX, vmod_riscv_machine* vrm, int idx)
{
	return forkcall(ctx, vrm, vrm->lookup.at(idx));
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
	assert(end_time.tv_sec == 0); /* We should never use seconds */
	return end_time.tv_nsec - start_time.tv_nsec;
}
