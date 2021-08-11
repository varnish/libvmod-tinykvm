
struct VMBuffer {
	const char *data;
	ssize_t size;
};

struct backend_result {
	const char *type;
	size_t  tsize;
	size_t  content_length;
	size_t  bufcount;
	struct VMBuffer buffers[0];
};
#define VMBE_NUM_BUFFERS  1024
#define VMBE_RESULT_SIZE  (sizeof(struct backend_result) + VMBE_NUM_BUFFERS * sizeof(struct VMBuffer))

struct backend_post {
	const struct vrt_ctx *ctx;
	struct vmod_kvm_machine *machine;
	union {
		uint64_t address;
		const void *ptr;
	};
	size_t length;
	uint64_t process_func;
	uint64_t func;
};
