#pragma once
#include <stdint.h>

#include "vtree.h"

#include "cache/cache_varnishd.h"
#include "cache/cache_director.h"
#include "cache/cache_filter.h"

#define RISCV_BACKEND_MAGIC 0x1f87a42d58b7426e

struct vmod_riscv_backend
{
	uint64_t magic;

	uint64_t max_instructions;

	struct director dir;
};

#define RISCV_MACHINE_MAGIC 0xb385716f486938e6

struct vmod_riscv_machine
{
	uint64_t magic;
};
typedef struct vmod_riscv_machine vmod_riscv_current_machine;
