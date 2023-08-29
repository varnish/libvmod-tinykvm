#include "settings.hpp"

#include "kvm_settings.h"

/* The current live settings. */
struct kvm_settings kvm_settings
{
	.backend_timings = 0,
};
