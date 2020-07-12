#include "script.hpp"

extern "C" {
# include "vdef.h"
# include "vrt.h"
	void *WS_Alloc(struct ws *ws, unsigned bytes);
}

struct vmod_riscv_machine {
	vmod_riscv_machine(std::vector<uint8_t> elf, VRT_CTX,
		uint64_t insn, uint64_t mem, uint64_t heap)
		: binary{std::move(elf)}, machine{binary},
		  max_instructions(insn), max_memory(mem), max_heap(heap) {}

	const std::vector<uint8_t> binary;
	riscv::Machine<riscv::RISCV32> machine;
	uint64_t max_instructions;
	uint64_t max_heap;
	uint64_t max_memory;
};
static std::vector<uint8_t> load_file(const std::string& filename);

extern "C"
vmod_riscv_machine* riscv_create(const char* file, VRT_CTX, uint64_t insn)
{
	return new vmod_riscv_machine(load_file(file), ctx,
		/* Max instr: */ insn, /* Mem: */ 8*1024*1024, /* Heap: */ 6*1024*1024);
}

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	/* Allocate Script on workspace, and construct it in-place */
	auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
	try {
		new (script) Script{vrm->machine, ctx,
			vrm->max_instructions, vrm->max_memory, vrm->max_heap};

	} catch (std::exception& e) {
		printf(">>> Exception: %s\n", e.what());
		return -1;
	}
	/* Call into the virtual machine */
	int ret = script->call(func);
	script->~Script(); /* call destructor */
	return ret;
}

extern "C"
int riscv_free(vmod_riscv_machine* vrm)
{
	delete vrm;
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
