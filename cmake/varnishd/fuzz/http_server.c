#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

extern void varnishd_initialize(const char* vcl_path);
extern const char* varnishd_client_path;

void http_fuzzer_server(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(
			"/home/gonzo/github/varnish_autoperf/vcl/response.vcl");
    }
    if (len == 0) return;

	struct sockaddr_un un_addr;
	memset(&un_addr, 0, sizeof(un_addr));
	un_addr.sun_family = AF_UNIX;
	strncpy(un_addr.sun_path, varnishd_client_path, sizeof(un_addr.sun_path) - 1);

    const int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

    int ret = connect(cfd, (struct sockaddr*) &un_addr, sizeof(un_addr));
    if (ret < 0) {
        close(cfd);
        fprintf(stderr, "Fuzzer connect() failed!?\n");
        sleep(1);
        return;
    }


    char drit[1024];
    int dritlen = snprintf(drit, sizeof(drit),
            "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-length: %zu\r\n\r\n",
            len);
    if (dritlen <= 0) {
        close(cfd);
        return;
    }

    ret = write(cfd, drit, dritlen);
    if (ret < 0) {
        close(cfd);
        return;
    }
    ret = write(cfd, data, len);
    if (ret < 0) {
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
    }

    close(cfd);
}
