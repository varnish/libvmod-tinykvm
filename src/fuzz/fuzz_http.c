#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_http(const char*);
extern uint16_t varnishd_client_port;

void http_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http(NULL);
    }


    const struct sockaddr_in sin_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(varnishd_client_port),
        .sin_addr   = { .s_addr = 0x0100007F }
    };
    if (len == 0) return;

    const int cfd =
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }
    int ret = connect(cfd, (struct sockaddr*) &sin_addr, sizeof(sin_addr));
    if (ret < 0) {
        close(cfd);
        fprintf(stderr, "Fuzzer connect() failed!?\n");
        sleep(1);
        return;
    }


    const char* req = "GET / HTTP/1.1\r\nHost: ";
    ret = write(cfd, req, strlen(req));
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
