#include "settings.hpp"

#include "kvm_settings.h"

/* The current live settings. */
struct kvm_settings kvm_settings
{
	.backend_early_release_size = 16384u,
	.backend_timings = false,
	.self_request_max_concurrency = 50,
};
