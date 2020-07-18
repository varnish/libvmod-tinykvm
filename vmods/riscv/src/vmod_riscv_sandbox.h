#pragma once
#include <stdint.h>

#define RISCV_MACHINE_MAGIC 0xb385716f486938e6

struct vmod_riscv_machine
{
	uint64_t magic;
};
typedef struct vmod_riscv_machine vmod_riscv_current_machine;
