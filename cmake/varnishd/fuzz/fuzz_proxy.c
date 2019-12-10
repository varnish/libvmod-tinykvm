#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_initialize(const char*);
extern bool varnishd_proxy_mode;

static const char proxy1_preamble[] = "PROXY ";
static const char proxy2_preamble[] = {
	0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A
};

void proxy_fuzzer(const void* data, size_t len, int version)
{
    static bool init = false;
    if (init == false) {
        init = true;
		varnishd_proxy_mode = true;
        varnishd_initialize(NULL);
    }
	if (len == 0) return;

    int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

	if (version == 1) {
		write(cfd, proxy1_preamble, sizeof(proxy1_preamble));
	}
	else if (version == 2) {
		write(cfd, proxy2_preamble, sizeof(proxy2_preamble));
	}

	const uint8_t* buffer = (uint8_t*) data;

	// do everything in at least two writes
	if (len > 0)
	{
		ssize_t bytes = write(cfd, buffer, len);
	    if (bytes < 0) {
	        close(cfd);
	        return;
	    }
		buffer += bytes;
		len    -= bytes;
		
	}
	
	const char* req = "\r\nGET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    int ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        //printf("Writing the request failed\n");
        close(cfd);
        return;
    }

    // signalling end of request, increases exec/s by 4x
    shutdown(cfd, SHUT_WR);

    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
        close(cfd);
        return;
    }

    close(cfd);
}
