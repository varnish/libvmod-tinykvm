#include <stdbool.h>
#include <stdint.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

extern void varnishd_initialize(const char* vcl_path);
extern int  open_varnishd_connection();
extern const char* varnishd_client_path;

extern uint32_t crc32(uint32_t crc, const char* buf, size_t len);
#define CLOSE(fuzzd) { close(fuzzd); fuzzd = 0; }
static __thread int fuzzd = 0;

void response_fuzzer(void* data, size_t len, int version)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize("vcl/fuzz_response.vcl");
    }
    if (len == 0) return;

    if (fuzzd == 0)
        fuzzd = open_varnishd_connection();
    if (fuzzd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

	char    gbuffer[1024];
	ssize_t gbuffer_len = 0;
	if (1 || version == 10) {
        const char gzip_header[] =
            ""; //"\x1f\x8b\x08\x00\x00\x00\x00\x00\x04\x03";
        const char header[] =
			"HTTP/1.1 200\r\n"
			"Connection: close\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"
			"Content-Encoding: br\r\n"
            "Content-Length: %zu\r\n"
			"\r\n%.*s";

        // HTTP header + gzip header
        gbuffer_len = sprintf(gbuffer, header,
            gbuffer_len,
            (int)sizeof(gzip_header)-1, gzip_header);
        // gzip footer
        gbuffer_len += 8;
	}

    char drit[100];
    int dritlen = snprintf(drit, sizeof(drit),
            "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %zu\r\n\r\n",
            len + gbuffer_len);
    assert(dritlen > 0);

    ssize_t ret = write(fuzzd, drit, dritlen);
    if (ret < 0) {
        CLOSE(fuzzd);
        return;
    }

	if (gbuffer_len > 0)
	{
		ret = write(fuzzd, data, len);
    	if (ret < 0) {
        	CLOSE(fuzzd);
        	return;
    	}

        struct {
            uint32_t crc;
            uint32_t len;
        } remainder;
        remainder.crc = crc32(~0, data, len);
        remainder.len = len;

        ret = write(fuzzd, &remainder, sizeof(remainder));
        if (ret < 0) {
            CLOSE(fuzzd);
            return;
        }
	} else {
        ret = write(fuzzd, data, len);
        if (ret < 0) {
            CLOSE(fuzzd);
            return;
        }
    }

    if (0) {
        // signalling end of request, increases exec/s by 4x
        shutdown(fuzzd, SHUT_WR);

    	// TODO: do we need this?
        char readbuf[2048];
        ssize_t rlen = read(fuzzd, readbuf, sizeof(readbuf));
        if (rlen < 0) {
            // Connection reset by peer just means varnishd closed early
            if (errno != ECONNRESET) {
                printf("Read failed: %s\n", strerror(errno));
            }
        }
    }
    //CLOSE(fuzzd);
}
