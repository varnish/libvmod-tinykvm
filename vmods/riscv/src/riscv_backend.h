#pragma once
#include <stdint.h>

struct VMBuffer {
	const char *data;
	ssize_t size;
};

struct backend_result {
	const char *type;
	uint16_t tsize; /* Max 64KB Content-Type */
	int16_t  status;
	size_t  content_length;
	size_t  bufcount;
	struct VMBuffer buffers[0];
};

#define VMBE_NUM_BUFFERS  1024
#define VMBE_RESULT_SIZE  (sizeof(struct backend_result) + VMBE_NUM_BUFFERS * sizeof(struct VMBuffer))
