#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern void register_func(int, ...);

/**
 * During the start of the program, one should register functions that will
 * handle different types of requests, like GET, POST and streaming POST.
 *
 * Example:
 *  int main()
 *  {
 *  	printf("Hello World\n"); // Will be logged
 *  	set_backend_get(my_get_request_handler);
 *  	wait_for_requests();
 *  }
 *
 * The example above will register a function called 'my_request_handler' as
 * the function that will get called on every single GET request. When the
 * main body is completed, we call 'wait_for_requests()' to signal that
 * the program has successfully initialized, and is ready to handle requests.
 *
 * Register callbacks for various modes of operations:
**/
static inline void set_on_recv(void(*f)(const char*)) { register_func(0, f); }
static inline void set_backend_get(void(*f)(const char*, const char*, int, int)) { register_func(1, f); }
static inline void set_backend_post(void(*f)(const char*, const uint8_t*, size_t)) { register_func(2, f); }
static inline void set_backend_stream_post(long(*f)(const uint8_t*, size_t)) { register_func(3, f); }

static inline void set_on_live_update(void(*f)()) { register_func(4, f); }
static inline void set_on_live_restore(void(*f)(size_t)) { register_func(5, f); }

/* Wait for requests without terminating machine. Call this just before
   the end of int main(). */
extern void wait_for_requests();

/**
 * When a request arrives the handler function is the only function that will
 * be called. main() is no longer in the picture. Any actions performed during
 * request handling will disappear after the request is processed.
 *
 * We will produce a response using the 'backend_response' function, which will
 * conclude the request.
 *
 * Example:
 *  static void my_request_handler(const char* url, int req, int resp)
 *  {
 *  	const char *ctype = "text/plain";
 *  	const char *cont = "Hello World";
 *  	backend_response(200, ctype, strlen(ctype), cont, strlen(cont));
 *  }
 **/
extern void __attribute__((noreturn, used))
backend_response(int16_t status, const void *t, uintptr_t, const void *c, uintptr_t);

static inline void
backend_response_str(int16_t status, const char *ctype, const char *content)
{
	backend_response(status, ctype, __builtin_strlen(ctype), content, __builtin_strlen(content));
}

/**
 * HTTP header field manipulation
 *
**/
extern void
http_append(int where, const char*, size_t);

static inline void
http_append_str(int where, const char *str) { http_append(where, str, __builtin_strlen(str)); }

/**
 * Varnish caching configuration
 *
**/
extern long
syscall_set_cacheable(int cached, long ttl_milliseconds);
static inline long set_cacheable(int cached, float ttl) {
	return syscall_set_cacheable(cached, ttl * 1000);
}

/**
 * Storage program
 *
 * Every tenant has a storage program that is initialized separately (and in
 * the same way as the main program) at the start. This program has a ton
 * of memory and is always available. Both the main and the storage programs
 * are running the same original program provided by the tenant at the start,
 * but they can diverge greatly over time.
 * To access storage you can use any of the API calls below when handling a
 * request. The access is serialized, meaning only one request can access the
 * storage at a time. Because of this, one should try to keep the number and
 * the duration of the calls low, preferrably calculating as much as possible
 * before making any storage accesses.
 *
 * When calling into the storage program the data you provide will be copied into
 * the VM, and the response you give back will be copied back into the request-
 * handling VM. This is extra overhead, but safe.
 * The purpose of the storage VM is to enable mutable state. That is, you are
 * able to make live changes to your storage VM, which persists across
 * requests. You can also serialize the state across live updates so that it
 * can be persisted when you have program updates. And finally, it can be
 * saved to a file (with a very specific name) on the servers disk, so that
 * it can be persisted across normal server updates and reboots.
**/

/* Vector-based serialized call into storage VM */
struct virtbuffer {
	void  *data;
	size_t len;
};
typedef void (*storage_func) (size_t n, struct virtbuffer[n], size_t res);

extern long
storage_call(storage_func, const void* src, size_t, void* dst, size_t);

extern long
storage_callv(storage_func, size_t n, const struct virtbuffer[n], void* dst, size_t);

static inline long
storage_call0(storage_func func) { return storage_call(func, NULL, 0, NULL, 0); }

/* Create an async task that is scheduled to run next in storage. The
   new task waits until other tasks are done before starting a new one,
   which will block the current thread, making this a blocking call. */
extern long
async_storage_task(void (*task)(void* arg), void* arg);

/* Used to return data from storage functions */
extern void
storage_return(const void* data, size_t len);

static inline void
storage_return_nothing(void) { storage_return(NULL, 0); }

/* Shared memory between all VMs. Globally visible to
   all VMs, and reads/writes are immediately seen
   by both storage and request VMs. */
struct shared_memory_info {
	void* ptr;
	uint64_t size;
};
extern struct shared_memory_info shared_memory_area();

/* If called during main() routine, it will cause
   global memory to be fully shared among everyone,
   not including the main stacks, which are separate.
   Effectively, memory will behave as if you are
   running a normal Linux program, and all requests
   were threads sharing the same memory. It is no
   longer necessary to call into storage, however it
   may still be useful to invoke async storage calls. */
extern void make_all_memory_shared();

/* From the point of calling this function, any new
   pages written to in the mutable storage will
   be globally visible immediately, shared with
   all request VMs in a one-sided way. Due to this
   one-sidedness, it has to be used with caution. */
extern void make_storage_memory_shared();

/* Start multi-processing using @n vCPUs on given function,
   forwarding up to 4 integral/pointer arguments.
   Multi-processing starts and ends asynchronously.
   The n argument is the number of total CPUs that will exist
   in the system, and n is required to be at least 2 to start
   one additional CPU. Use vcpuid() to retrieve the current
   vCPU id during asynchronous operation.

   Example usage:
	// Start 7 additional vCPUs, each running the given function with the
	// same provided argument:
	multiprocess(8, (multiprocess_t)dotprod_mp_avx, &data);
	// Run the first portion on the current vCPU (with id 0):
	dotprod_mp_avx(&data);
	// Wait for the asynchronous multi-processing operation to complete:
	multiprocess_wait();
*/
typedef void(*multiprocess_t)(void*);
extern long multiprocess(size_t n, multiprocess_t func, void*);

/* Start multi-processing using @n vCPUs on given function,
   forwarding an array with the given array element size.
   The array must outlive the vCPU asynchronous operation.

   Example usage:
	// Start 7 additional vCPUs
	multiprocess_array(8, dotprod_mp_avx, &data, sizeof(data[0]));
	// Run the first portion on the main vCPU (with id 0)
	dotprod_mp_avx(&data[0], sizeof(data[0]));
	// Wait for the asynchronous operation to complete
	multiprocess_wait();
*/
typedef void(*multiprocess_array_t)(int, void* array, size_t element_size);
extern long multiprocess_array(size_t n,
	multiprocess_array_t func, void* array, size_t element_size);

/* Start multi-processing using @n vCPUs at the current RIP,
   forwarding all registers except RFLAGS, RSP, RBP, RAX, R14, R15.
   Those registers are clobbered and have undefined values.

   Stack size is the size of one individual stack, and the caller
   must make room for n stacks of the given size. Example:
   void* stack_base = malloc(n * stack_size);
   multiprocess_clone(n, stack_base, stack_size);
*/
extern long multiprocess_clone(size_t n, void* stack_base, size_t stack_size);

/* Wait until all multi-processing workloads have ended running. */
extern long multiprocess_wait();

/* Returns the current vCPU ID. Used in processing functions during
   multi-processing operation. */
extern int vcpuid() __attribute__((const));

/* This cannot be used when KVM is used as a backend */
#ifndef KVM_API_ALREADY_DEFINED
#define DYNAMIC_CALL(name, hash, ...) \
	asm(".global " #name "\n" \
	#name ":\n" \
	"	mov $" #hash ", %eax\n" \
	"	out %eax, $1\n" \
	"   ret\n"); \
	extern long name(__VA_ARGS__);
#else
#define DYNAMIC_CALL(name, hash, ...) \
	extern long name(__VA_ARGS__);
#endif
DYNAMIC_CALL(goto_dns, 0x746238D2)

/* Fetch content from provided URL. Content must be allocated prior to
   calling the function. The fetcher will fill out the structure if the
   fetch succeeds. If a serious error is encountered, the function returns
   a non-zero value and the struct contents is undefined. */
struct curl_opts {
	long   status;
	size_t      post_buflen;
	const void *post_buffer;
	size_t content_length;
	void  *content;
	size_t ctlen;
	char   ctype[256];
};
DYNAMIC_CALL(curl_fetch, 0xB86011FB, const char*, size_t, struct curl_opts*)

/* Embed binary data into executable. This data has no guaranteed alignment. */
#define EMBED_BINARY(name, filename) \
	asm(".section .rodata\n" \
	"	.global " #name "\n" \
	#name ":\n" \
	"	.incbin " #filename "\n" \
	#name "_end:\n" \
	"	.int  0\n" \
	"	.global " #name "_size\n" \
	"	.type   " #name "_size, @object\n" \
	"	.align 4\n" \
	#name "_size:\n" \
	"	.int  " #name "_end - " #name "\n" \
	".section .text"); \
	extern char name[]; \
	extern unsigned name ##_size;

#define TRUST_ME(ptr)    ((void*)(uintptr_t)(ptr))

#ifndef KVM_API_ALREADY_DEFINED
asm(".global register_func\n" \
".type register_func, function\n" \
"register_func:\n" \
"	mov $0x10000, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global wait_for_requests\n" \
".type wait_for_requests, function\n" \
"wait_for_requests:\n" \
"	mov $0x10001, %eax\n" \
"	out %eax, $0\n");

asm(".global syscall_set_cacheable\n" \
".type syscall_set_cacheable, function\n" \
"syscall_set_cacheable:\n" \
"	mov $0x10005, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global http_append\n" \
".type http_append, function\n" \
"http_append:\n" \
"	mov $0x10020, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global backend_response\n" \
".type backend_response, function\n" \
"backend_response:\n" \
"	mov $0xFFFF, %eax\n" \
"	out %eax, $0\n");

asm(".global shared_memory_area\n" \
".type shared_memory_area, function\n" \
"shared_memory_area:\n" \
"	mov $0x10700, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global make_storage_memory_shared\n" \
".type make_storage_memory_shared, function\n" \
"make_storage_memory_shared:\n" \
"	mov $0x10701, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global make_all_memory_shared\n" \
".type make_all_memory_shared, function\n" \
"make_all_memory_shared:\n" \
"	mov $0x10702, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global storage_call\n" \
".type storage_call, function\n" \
"storage_call:\n" \
"	mov $0x10707, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global storage_callv\n" \
".type storage_callv, function\n" \
"storage_callv:\n" \
"	mov $0x10708, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global async_storage_task\n" \
".type async_storage_task, function\n" \
"async_storage_task:\n" \
"	mov $0x10709, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global storage_return\n" \
".type storage_return, function\n" \
"storage_return:\n" \
"	mov $0xFFFF, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global multiprocess\n" \
".type multiprocess, function\n" \
"multiprocess:\n" \
"	mov $0x10710, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global multiprocess_array\n" \
".type multiprocess_array, function\n" \
"multiprocess_array:\n" \
"	mov $0x10711, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global multiprocess_clone\n" \
".type multiprocess_clone, function\n" \
"multiprocess_clone:\n" \
"	mov $0x10712, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global multiprocess_wait\n" \
".type multiprocess_wait, function\n" \
"multiprocess_wait:\n" \
"	mov $0x10713, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global vcpuid\n" \
".type vcpuid, function\n" \
"vcpuid:\n" \
"	mov %gs:(0x0), %eax\n" \
"   ret\n");
#endif

#ifdef __cplusplus
}
#endif
