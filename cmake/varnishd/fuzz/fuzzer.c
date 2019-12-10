#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

// 1. connect using TCP socket and send requests
// 2. generate files for ESI parser
// 3. VCC fuzzing

extern void http_fuzzer(void* data, size_t len);
extern void http_fuzzer_server(void* data, size_t len);
extern void h2_fuzzer(void* data, size_t len);
extern void proxy_fuzzer(void* data, size_t len, int version);
extern void hpack_fuzzer(void* data, size_t len);

// varnishd has many many leaks.. can't enable this
int __lsan_is_turned_off() { return 1; }

#include <sys/prctl.h>
__attribute__((constructor))
void make_dumpable() {
	prctl(PR_SET_DUMPABLE, 1);
}

int LLVMFuzzerTestOneInput(void* data, size_t len)
{
#ifdef FUZZER_HTTP
    http_fuzzer(data, len);
#elif defined(FUZZER_HTTP_SERVER)
    http_fuzzer_server(data, len);
#elif defined(FUZZER_HTTP2)
    h2_fuzzer(data, len);
#elif defined(FUZZER_PROXY)
	proxy_fuzzer(data, len, 1);
#elif defined(FUZZER_PROXY2)
	proxy_fuzzer(data, len, 2);
#elif defined(FUZZER_HPACK)
    hpack_fuzzer(data, len);
#elseif defined(FUZZER_)
	static_assert(false, "The fuzzer type was not defined before building!");
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
