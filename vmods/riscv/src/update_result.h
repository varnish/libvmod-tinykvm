#pragma once
#include <stddef.h>

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
