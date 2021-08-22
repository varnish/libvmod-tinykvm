#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

asm(".global backend_response\n" \
".type backend_response, function\n" \
"backend_response:\n" \
"	mov $0xFFFF, %eax\n" \
"	out %eax, $0\n");

asm(".global storage_callv\n" \
".type storage_callv, function\n" \
"storage_callv:\n" \
"	mov $0x10708, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global storage_return\n" \
".type storage_return, function\n" \
"storage_return:\n" \
"	mov $0xFFFF, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global vmcommit\n" \
".type vmcommit, function\n" \
"vmcommit:\n" \
"	mov $0x1070A, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

/* Use this to create a backend response from a KVM backend */
extern void __attribute__((noreturn))
backend_response(uint16_t, const void *t, uint64_t, const void *c, uint64_t);

static inline
void return_result(uint16_t code, const char *ctype, const char *content)
{
	backend_response(code, ctype, strlen(ctype), content, strlen(content));
}

/* Vector-based serialized call into storage VM */
struct virtbuffer {
	void  *data;
	size_t len;
};
typedef void (*storage_func) (size_t n, struct virtbuffer[n], size_t res);

extern long
storage_callv(storage_func, size_t n, const struct virtbuffer[n], void* dst, size_t);

extern void
storage_return(const void* data, size_t len);

static inline void
storage_return_nothing(void) { storage_return(NULL, 0); }

/* Record the current state into a new VM, and make that
   VM handle future requests. WARNING: RCU, Racey */
extern long vmcommit(void);

/* This cannot be used when KVM is used as a backend */
#define DYNAMIC_CALL(name, hash) \
	asm(".global " #name "\n" \
	#name ":\n" \
	"	mov " #hash ", %eax\n" \
	"	out %eax, $1\n" \
	"   ret\n"); \
	extern long name();
DYNAMIC_CALL(goto_dns, 0x746238D2)

/* Embed binary data into executable */
#define EMBED_BINARY(name, filename) \
	asm(".section .rodata\n" \
	"	.global " #name "\n" \
	#name ":\n" \
	"	.incbin " #filename "\n" \
	#name "_end:\n" \
	"	.global " #name "_size\n" \
	"	.type   " #name "_size, @object\n" \
	"	.align 4\n" \
	#name "_size:\n" \
	"	.int  " #name "_end - " #name "\n" \
	".section .text"); \
	extern char name[]; \
	extern unsigned name ##_size;

#define TRUST_ME(ptr)    ((void*)(uintptr_t)(ptr))

#ifdef __cplusplus
}
#endif
