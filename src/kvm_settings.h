#pragma once
#include <stdint.h>

struct kvm_settings {
	size_t backend_early_release_size;
	int backend_timings;
	int self_request_max_concurrency;
};
extern struct kvm_settings kvm_settings;
