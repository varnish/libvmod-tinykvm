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

//#define VMOD_COOKIEPLUS
//#define VMOD_URLPLUS
//#define VMOD_WAF

void vmod_fuzzer(uint8_t* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(
			"/home/gonzo/github/varnish_autoperf/vcl/vmod_fuzz.vcl"
		);
    }
	if (len == 0) return;

    int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

	if (len > 0)
	{
#ifdef VMOD_COOKIEPLUS
		const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1";
		int ret = write(cfd, req, strlen(req));
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}

		const char* seq = "\r\nCookie: test=";
	    int ret = write(cfd, seq, strlen(seq));
	    if (ret < 0) {
	        //printf("Writing the request failed\n");
	        close(cfd);
	        return;
	    }

		ssize_t bytes = write(cfd, data, len);
	    if (bytes < 0) {
	        close(cfd);
	        return;
	    }
		write(cfd, "\r\n\r\n", 4);
#elif defined(VMOD_URLPLUS)
		const char* req = "GET /";
		int ret = write(cfd, req, strlen(req));
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}

		ret = write(cfd, data, len);
		if (ret < 0) {
			close(cfd);
			return;
		}
		
		req = " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
		ret = write(cfd, req, strlen(req));
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}
#elif defined(VMOD_WAF)
		char buffer[5000];
		int L = snprintf(buffer, sizeof(buffer),
				"GET /%.*s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
				"Content-Type: text/html\r\n"
				"Content-Length: %zu\r\n"
				"\r\n", (int) len, data, len);
		int ret = write(cfd, buffer, L);
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}

		ssize_t bytes = write(cfd, data, len);
		if (bytes < 0) {
			close(cfd);
			return;
		}
#else
		const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nInput: ";
		int ret = write(cfd, req, strlen(req));
		if (ret < 0) {
			//printf("Writing the request failed\n");
			close(cfd);
			return;
		}

		ssize_t bytes = write(cfd, data, len);
		if (bytes < 0) {
			close(cfd);
			return;
		}
#endif
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
