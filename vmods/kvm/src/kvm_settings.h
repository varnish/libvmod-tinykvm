#pragma once

struct kvm_settings {
	int backend_timings;
	int self_request_max_concurrency;
};
extern struct kvm_settings kvm_settings;
