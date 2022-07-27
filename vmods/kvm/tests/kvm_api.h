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
 *   static void on_get(const char* url)
 *   {
 *      http_setf(BERESP, "X-Hello: World", 14);
 *      backend_response_str(200, "text/plain", "Hello World!");
 *   }
 *   int main()
 *   {
 *  	set_backend_get(on_get);
 *  	wait_for_requests();
 *   }
 *
 * The example above will register a function called 'my_request_handler' as
 * the function that will get called on every single GET request. When the
 * main body is completed, we call 'wait_for_requests()' to signal that
 * the program has successfully initialized, and is ready to handle requests.
 * The system will then pause the VM and freeze everything. The frozen state
 * will be restored on every request.
 *
 * Register callbacks for various modes of operation:
 **/
static inline void set_backend_get(void(*f)(const char *url)) { register_func(1, f); }
static inline void set_backend_post(void(*f)(const char *url, const uint8_t*, size_t)) { register_func(2, f); }

/* Streaming POST will receive each data segment as they arrive. A final POST
   call happens at the end. This call needs some further improvements, because
   right now Varnish will fill the VM with the whole POST data no matter what,
   but call the streaming POST callback on each segment. Instead, it should put
   each segment on stack and the callee may choose to build a complete buffer. */
static inline void set_backend_stream_post(long(*f)(const char *url, const uint8_t *data, size_t len, size_t off)) { register_func(3, f); }

/* When uploading a new program, there is an opportunity to pass on
   state to the next program, using the live update and restore callbacks. */
static inline void set_on_live_update(void(*f)()) { register_func(4, f); }
static inline void set_on_live_restore(void(*f)(size_t datalen)) { register_func(5, f); }

/* When an exception happens that terminates the request it is possible to
   produce a custom response instead of a generic HTTP 500. There is very
   limited time to produce the response, typically 1.0 seconds. */
static inline void set_on_error(void (*f)(const char *url, const char *exception)) { register_func(6, f); }

/* Wait for requests without terminating machine. Call this just before
   the end of int main(). It will preserve the state of the whole machine,
   including startup arguments and stack values. Future requests will use
   a new stack, and will not trample the old stack, including red-zone. */
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
 *  static void on_get(const char* url, const char *arg, int req, int resp)
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
static const int BEREQ  = 4;  /* Get values from this. */
static const int BERESP = 5;  /* Set values on this. */

extern void
http_appendf(int where, const char*, size_t);

/* Append a new header field. */
static inline void
http_append_str(int where, const char *str) { http_appendf(where, str, __builtin_strlen(str)); }

/* Set or overwrite an existing header field. */
extern long
http_setf(int where, const char *what, size_t len);

/* Unset an existing header field by key. */
static inline long
http_unsetf(int where, const char *key, size_t len) { return http_setf(where, key, len); }

/* Retrieve a header field by key. */
extern long
http_findf(int where, const char *key, size_t, const char *outb, size_t outl);

/**
 * Varnish caching configuration
 *
**/
extern long
syscall_set_cacheable(int cached, long ttl_millis, long grace_ms, long keep_ms);
static inline long set_cacheable(int cached, float ttl, float grace, float keep) {
	return syscall_set_cacheable(cached, ttl * 1000, grace * 1000, keep * 1000);
}

/**
 * Storage program
 *
 * Every tenant has storage. Storage is the program itself as it was
 * initialized at the start. If your tenant has 1GB memory, then storage is
 * 1GB of memory too. Request VMs are based off of the state of
 * storage, even as it changes. Storage is just like a normal Linux program
 * in that any changes you make are never unmade. This program has a ton
 * of memory and is always available.
 * To access storage you can use any of the API calls below when handling a
 * request. The access is serialized, meaning only one request can access the
 * storage at a time. Because of this, one should try to keep the number and
 * the duration of the calls low, preferrably calculating as much as possible
 * before making any storage accesses.
 * It is also possible to access storage only to make persistent changes, while
 * accessing global variables in the request VMs as needed afterwards. To
 * enable this mode call make_storage_memory_shared() from main. Be cautious
 * of race conditions, as always.
 *
 * When calling into the storage program the data you provide will be copied into
 * the VM, and the response you give back will be copied back into the request-
 * handling VM. This is extra overhead, but safe.
 * The purpose of the storage VM is to enable mutable state. That is, you are
 * able to make live changes to your storage, which persists across time and
 * requests. You can also serialize the state across live updates so that it
 * can be persisted through program updates. And finally, it can be
 * saved to a file (with a very specific name) on the servers disk, so that
 * it can be persisted across normal server updates and reboots.
 **/

/* Vector-based serialized call into storage VM */
struct virtbuffer {
	void  *data;
	size_t len;
};
typedef void (*storage_func) (size_t n, struct virtbuffer[n], size_t res);

/* Transfer an array to storage, transfer output into @dst. */
extern long
storage_call(storage_func, const void *src, size_t, void *dst, size_t);

/* Transfer an array of buffers to storage, transfer output into @dst. */
extern long
storage_callv(storage_func, size_t n, const struct virtbuffer[n], void* dst, size_t);

/* Create a task in storage that is scheduled to run next. The new
   task waits until other tasks are done before starting a new one,
   which will block the current thread, making this a blocking call.
   If start or period is set, the task will be scheduled to run after
   start milliseconds, and then run every period milliseconds. The
   system call returns the timer id.
   If async is enabled, it is possible re-enter storage. NB: Watch out
   for race conditions!
   If it is a periodic task, it will return a task id. */
extern long
sys_storage_task(void (*task)(void* arg), void* arg, int async, uint64_t start, uint64_t period);

/* Schedule a storage task to happen outside of request handling. */
static inline long
storage_task(void (*task)(void *arg), void *arg) { return sys_storage_task(task, arg, 0, 0, 0); }

/* Schedule a storage task to happen at some point outside of request handling. */
static inline long
schedule_storage_task(void (*task)(void *arg), void *arg, float start, float period) {
	return sys_storage_task(task, arg, 0, start * 1000, period * 1000);
}

/* Async storage tasks happen even while storage is entered somewhere
   else. It is a re-entrant call, so watch out for race conditions.
   This functions allows scheduling work from storage even if it
   very busy in ordinary requests, or you are fetching directly
   from Varnish where the request would try to enter storage. */
static inline long
async_storage_task(void (*task)(void *arg), void *arg) { return sys_storage_task(task, arg, 1, 0, 0); }

/* Stop a previously scheduled task. Returns TRUE on success. */
extern long
stop_storage_task(long task);

/* Used to return data from storage functions. */
extern void
storage_return(const void* data, size_t len);

static inline void
storage_return_nothing(void) { storage_return(NULL, 0); }

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


/* Shared memory between all VMs. Globally visible to
   all VMs, and reads/writes are immediately seen
   by both storage and request VMs. */
struct shared_memory_info {
	uint64_t ptr;
	uint64_t end;
};
extern struct shared_memory_info shared_memory_area();

/* Allocate pointers to shared memory with given size and alignment. */
#define SHM_ALLOC_BYTES(x) internal_shm_alloc(x, 8)
#define SHM_ALLOC_TYPE(x) internal_shm_alloc(sizeof(x), _Alignof(x))
static inline void * internal_shm_alloc(size_t size, size_t align) {
	static struct shared_memory_info info;
	if (info.ptr == 0x0) {
		info = shared_memory_area();
	}
	char *p = (char *)((info.ptr + (align-1)) & ~(uint64_t)(align-1));
	info.ptr = (uint64_t)&p[size];
	if ((uint64_t)p + size <= info.end)
		return p;
	else
		return NULL;
}

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

/* Setting this during initialization will determine whether
   or not request VMs will be reset after each request.
   When they are ephemeral, they will be reset.
   This setting is ENABLED by default for security reasons. */
extern int make_ephemeral(int);

/* Retrieve memory layout information. */
struct meminfo {
	uint64_t max_memory;
	uint64_t max_reqmem;
	uint64_t reqmem_upper;
	uint64_t reqmem_current;
};
extern void get_meminfo(struct meminfo*);

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

/* Fetch content from provided URL. Content will be allocated by Varnish
   when fetching. The fetcher will also fill out the input structure if the
   fetch succeeds. If a serious error is encountered, the function returns
   a non-zero value and the struct contents are undefined.
   *curl_fields* and *curl_options* are both optional and can be NULL.
   NOTE: It is possible to run out of memory in mutable storage if you do a
   lot of cURL fetching without unmapping the content buffer. */
struct curl_op {
	uint32_t    status;
	uint32_t    post_buflen;
	const void *post_buffer;
	void  *content;
	uint32_t content_length;
	uint32_t ctlen;
	char   ctype[128];
};
struct curl_fields {
	const char *ptr[8];
	uint16_t    len[8];
};
struct curl_options {
	const char *interface;      /* Select interface to bind to. */
	const char *unused;
	int8_t      dummy_fetch;    /* Does not allocate content. */
	int8_t      tcp_fast_open;  /* Enables TCP Fast Open. */
	int8_t      unused_opt1;
	int8_t      unused_opt2;
	uint32_t    unused_opt3;
};
DYNAMIC_CALL(curl_fetch, 0xB86011FB, const char*, size_t, struct curl_op*, struct curl_fields*, struct curl_options*)

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

asm(".global http_appendf\n" \
".type http_appendf, function\n" \
"http_appendf:\n" \
"	mov $0x10020, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global http_setf\n" \
".type http_setf, function\n" \
"http_setf:\n" \
"	mov $0x10021, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global http_findf\n" \
".type http_findf, function\n" \
"http_findf:\n" \
"	mov $0x10022, %eax\n" \
"	out %eax, $0\n" \
"	ret");

asm(".global backend_response\n" \
".type backend_response, function\n" \
"backend_response:\n" \
"	mov $0x10010, %eax\n" \
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

asm(".global make_ephemeral\n" \
".type make_ephemeral, function\n" \
"make_ephemeral:\n" \
"	mov $0x10703, %eax\n" \
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

asm(".global sys_storage_task\n" \
".type sys_storage_task, function\n" \
"sys_storage_task:\n" \
"	mov $0x10709, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");

asm(".global stop_storage_task\n" \
".type stop_storage_task, function\n" \
"stop_storage_task:\n" \
"	mov $0x1070A, %eax\n" \
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

asm(".global get_meminfo\n" \
".type get_meminfo, function\n" \
"get_meminfo:\n" \
"	mov $0x10A00, %eax\n" \
"	out %eax, $0\n" \
"   ret\n");
#endif // KVM_API_ALREADY_DEFINED

#ifdef __cplusplus
}
#endif
