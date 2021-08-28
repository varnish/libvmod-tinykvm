#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_initialize(const char*);
extern int  open_varnishd_connection();
extern bool varnishd_proxy_mode;

static const char proxy1_preamble[] = "PROXY ";
static const char proxy2_preamble[] = {
//	0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A
};

void proxy_fuzzer(const void* data, size_t len, int version)
{
    static bool init = false;
    if (init == false) {
        init = true;
		fprintf(stderr, "LIBFUZZER: Proxy protocol version %d\n", version);
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

	char request[12000];
	int  reqlen = 0;

	if (version == 1) {
		reqlen +=
		snprintf(&request[0], sizeof(request),
				"%.*s", (int) sizeof(proxy1_preamble), proxy1_preamble);
	}
	else if (version == 2) {
		reqlen +=
		snprintf(&request[0], sizeof(request),
				"%.*s", (int) sizeof(proxy2_preamble), proxy2_preamble);
	}

	reqlen += snprintf(&request[reqlen], sizeof(request) - reqlen,
						"%.*s", (int) len, data);

	// some HTTP
	reqlen += snprintf(&request[reqlen], sizeof(request) - reqlen,
						"\r\nGET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

	if (version == 1) {
		for (int i = 0; i < 2; i++)
		{
			int ret = write(cfd, request, reqlen);
		    if (ret < 0) {
		        //printf("Writing the request failed\n");
		        close(cfd);
		        return;
		    }
			//usleep(300*1000);
		}
	} else {
		int ret = write(cfd, request, reqlen);
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}
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
