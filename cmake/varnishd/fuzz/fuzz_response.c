#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

extern void varnishd_initialize(const char* vcl_path);
extern int  open_varnishd_connection();
extern const char* varnishd_client_path;

void response_fuzzer(void* data, size_t len, int version)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(
			"/home/gonzo/github/varnish_autoperf/vcl/response.vcl");
    }
    if (len == 0) return;

    const int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

	char    gbuffer[1024];
	ssize_t gbuffer_len = 0;
	if (version == 10) {
		gbuffer_len = snprintf(gbuffer, sizeof(gbuffer),
			"HTTP/1.1 200\r\n"
			"Connection: close\r\n"
			"Content-Type: text/html; charset=UTF-8\r\n"
			"Content-Encoding: gzip\r\n"
			"\r\n");
	}
	
    char drit[200];
    int dritlen = snprintf(drit, sizeof(drit),
            "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-length: %zu\r\n\r\n",
            len + gbuffer_len);
    if (dritlen <= 0) {
        close(cfd);
        return;
    }

    ssize_t ret = write(cfd, drit, dritlen);
    if (ret < 0) {
        close(cfd);
        return;
    }

	if (gbuffer_len > 0)
	{
		ret = write(cfd, data, len);
    	if (ret < 0) {
        	close(cfd);
        	return;
    	}
	}
	
    ret = write(cfd, data, len);
    if (ret < 0) {
        close(cfd);
        return;
    }
    // signalling end of request, increases exec/s by 4x
    shutdown(cfd, SHUT_WR);

	// TODO: do we need this?
    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
    }

    close(cfd);
}
