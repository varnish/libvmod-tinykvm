#include <stdint.h>

static const uint64_t POST_BUFFER = (1UL << 28); /* 256MB */

struct VMBuffer {
	const char *data;
	ssize_t size;
};
struct vsl_log;

struct vmod_kvm_backend
{
	uint64_t magic;
	struct director *dir;

	struct vmod_kvm_tenant *tenant;
	const char *funcarg[2];
	int debug;
	uint64_t max_response_size;
	const struct director* backend;

	double t_prev;
	double t_work;
};

struct backend_result {
	const char *type;
	uint16_t tsize; /* Max 64KB Content-Type */
	int16_t  status;
	/* When content length > 0 and bufcount == 0, it is a streamed response. */
	size_t  content_length;
	size_t  bufcount;
	union {
		/* The result is either a list of buffers. */
		struct VMBuffer buffers[0];
		/* Or, it's a streamed response. */
		struct {
			struct vmod_kvm_slot *stream_slot;
			struct vsl_log *stream_vsl;
			uint64_t stream_callback;
			uint64_t stream_argument;
		};
	};
};
#define VMBE_NUM_BUFFERS  512
#define VMBE_RESULT_SIZE  (sizeof(struct backend_result) + VMBE_NUM_BUFFERS * sizeof(struct VMBuffer))

struct backend_post {
	const struct vrt_ctx *ctx;
	struct vmod_kvm_slot *slot;
	uint64_t address;
	uint64_t capacity;
	size_t length;
	const char *argument;
};
