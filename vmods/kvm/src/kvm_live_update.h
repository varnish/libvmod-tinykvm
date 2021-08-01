#pragma once
#include <stddef.h>

struct update_params {
	const uint8_t* data;
	const size_t len;
	const int16_t is_debug;
	const int16_t debug_port;
};

struct update_result {
	const char* output;
	const size_t len;
	void(*destructor) (struct update_result*);
};

typedef struct {
	int idx;
	int arg1;
	int arg2;
} vcall_info;
