#pragma once
#include <stdint.h>

struct kvm_settings {
	size_t backend_early_release_size;
	int backend_timings;
	int self_request_max_concurrency;
	/* Call directly into leased VMs on the calling thread, instead of
	   messaging the VMs dedicated thread. -1: Auto (kernel 6.13+),
	   0: Disabled, 1: Enabled. */
	int direct_vmcalls;
	/* Threads with less stack than this fall back to the VMs thread. */
	size_t direct_vmcall_min_stack;
};
extern struct kvm_settings kvm_settings;
