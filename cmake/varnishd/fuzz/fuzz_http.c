#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_http(const char*);

void http_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http(NULL);
    }
	if (len == 0) return;

    int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

    const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1";
    int ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        //printf("Writing the request failed\n");
        close(cfd);
        return;
    }

	const uint8_t* buffer = (uint8_t*) data;

	// do everything in at least two writes
	if (len > 0)
	{
		const uint8_t prelen = buffer[0];
		buffer ++;
		len --;
		if (len >= prelen) {
			ssize_t bytes = write(cfd, buffer, prelen);
		    if (bytes < 0) {
		        close(cfd);
		        return;
		    }
			buffer += bytes;
			len    -= bytes;
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
