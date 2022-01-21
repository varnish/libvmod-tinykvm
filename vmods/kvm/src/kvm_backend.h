
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

struct backend_post {
	const struct vrt_ctx *ctx;
	struct vmod_kvm_slot *slot;
	uint64_t address;
	size_t length;
	uint64_t process_func;
	uint64_t func;
};
