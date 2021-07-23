#pragma once
#include <stdint.h>

#include "vtree.h"

#include "cache/cache_varnishd.h"
#include "cache/cache_director.h"
#include "cache/cache_filter.h"

#define KVM_BACKEND_MAGIC 0x645603935c082fa6

struct vmod_kvm_backend
{
	uint64_t magic;

	uint64_t max_time;

	struct director dir;
};

struct vmod_kvm_updater
{
	uint64_t magic;
	struct director dir;

	uint64_t max_binary_size;
	struct vmod_kvm_machine *machine;
	int16_t  is_debug;
	uint16_t debug_port;
};

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
struct vmod_kvm_response
{
	uint64_t magic;
	struct director dir;

	const void*    priv_key;
	struct vmod_kvm_machine *machine;
	uint64_t funcaddr;
	uint64_t funcarg;
	uint64_t max_response_size;
};
