#pragma once
#include <stddef.h>

struct update_params {
	const uint8_t* data;
	const size_t len;
	const int is_debug;
};

struct update_result {
	const char* output;
	const size_t len;
	void(*destructor) (struct update_result*);
};

typedef struct {
	unsigned idx;
	int arg1;
	int arg2;
} vcall_info;
