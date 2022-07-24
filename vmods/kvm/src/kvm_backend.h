#include <stdint.h>

static const uint64_t POST_BUFFER = (1UL << 28); /* 256MB */

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
#define VMBE_NUM_BUFFERS  2048
#define VMBE_RESULT_SIZE  (sizeof(struct backend_result) + VMBE_NUM_BUFFERS * sizeof(struct VMBuffer))

struct backend_post {
	const struct vrt_ctx *ctx;
	struct vmod_kvm_slot *slot;
	uint64_t address;
	uint64_t capacity;
	size_t length;
	const char *argument;
};
