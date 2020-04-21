#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
//#define USE_THREADPOOL

// 1. connect using TCP socket and send requests
// 2. generate files for ESI parser
// 3. VCC fuzzing

extern void http_fuzzer(void* data, size_t len);
extern void response_fuzzer(void* data, size_t len, int version);
extern void h2_fuzzer(void* data, size_t len);
extern void proxy_fuzzer(void* data, size_t len, int version);
extern void hpack_fuzzer(void* data, size_t len);
extern void vmod_fuzzer(void* data, size_t len);

// varnishd has many many leaks.. can't enable this
int __lsan_is_turned_off() { return 1; }
// abort on errors ASAP
void __asan_on_error() {
	void abort(void) __attribute__((noreturn));
	abort();
}

#include <sys/prctl.h>
__attribute__((constructor))
void make_dumpable() {
	prctl(PR_SET_DUMPABLE, 1);
}

static inline
void fuzz_one(void* data, size_t len)
{
#if defined(FUZZER_HTTP) || defined(FUZZER_HTTP1)
    http_fuzzer(data, len);
#elif defined(FUZZER_HTTP2)
    h2_fuzzer(data, len);
#elif defined(FUZZER_RESPONSE_H1)
    response_fuzzer(data, len, 1);
#elif defined(FUZZER_RESPONSE_H2)
    response_fuzzer(data, len, 2);
#elif defined(FUZZER_RESPONSE_GZIP)
    response_fuzzer(data, len, 10);
#elif defined(FUZZER_PROXY) || defined(FUZZER_PROXY1)
	proxy_fuzzer(data, len, 1);
#elif defined(FUZZER_PROXY2)
	proxy_fuzzer(data, len, 2);
#elif defined(FUZZER_HPACK)
    hpack_fuzzer(data, len);
#elif defined(FUZZER_VMOD)
    vmod_fuzzer(data, len);
#else
	static_assert(false, "The fuzzer type was not recognized!");
#endif
}

#ifdef USE_THREADPOOL
#include <thpool.h>
#include <stdatomic.h>
#include <x86intrin.h>
#define THREADPOOL_SIZE 4
#define WORK_MAX 6000
static threadpool tpool;
static volatile int work_counter;
struct tpool_work {
	size_t len;
	char   data[0];
};
static void tpool_fuzz_one(void* arg)
{
	struct tpool_work* work = (struct tpool_work*) arg;
	fuzz_one(work->data, work->len);
	free(work);
	__sync_fetch_and_sub(&work_counter, 1);
}
#endif

int LLVMFuzzerTestOneInput(void* data, size_t len)
{
#ifdef USE_THREADPOOL
	static bool init = false;
    if (init == false) {
        init = true;
		// make sure varnishd is initialized
		fuzz_one(NULL, 0);
		tpool = thpool_init(THREADPOOL_SIZE);
	}
	for (int i = 0; i < THREADPOOL_SIZE; i++)
	{
		while (work_counter >= WORK_MAX) {
			_mm_pause();
		}
		const size_t total = sizeof(struct tpool_work) + len;
		struct tpool_work* work = malloc(total);
		work->len = len;
		memcpy(work->data, data, len);
		__sync_fetch_and_add(&work_counter, 1);
		thpool_add_work(tpool, tpool_fuzz_one, work);
	}
#else
	fuzz_one(data, len);
#endif
    return 0;
}

#include <sys/socket.h>
#include <sys/un.h>
extern const char* varnishd_client_path;

int open_varnishd_connection()
{
	struct sockaddr_un un_addr;
	memset(&un_addr, 0, sizeof(un_addr));
	un_addr.sun_family = AF_UNIX;
	strncpy(un_addr.sun_path, varnishd_client_path, sizeof(un_addr.sun_path) - 1);

    const int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return -1;
    }
    int ret = connect(cfd, (const struct sockaddr*) &un_addr, sizeof(un_addr));
    if (ret < 0) {
        close(cfd);
        fprintf(stderr, "Fuzzer connect() failed!?\n");
        sleep(1);
        return -1;
    }

	return cfd;
}
