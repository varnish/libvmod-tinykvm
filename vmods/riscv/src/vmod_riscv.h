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

struct vmod_riscv_updater
{
	uint64_t magic;
	struct director dir;

	uint64_t max_binary_size;
	struct vmod_riscv_machine *machine;
	int16_t  is_debug;
	uint16_t debug_port;
};

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
struct vmod_riscv_response
{
	uint64_t magic;
	struct director dir;

	const void*    priv_key;
	struct vmod_riscv_machine *machine;
	uint64_t funcaddr;
	uint64_t funcarg;
	uint64_t max_response_size;
};
