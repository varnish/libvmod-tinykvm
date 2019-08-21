#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

extern void varnishd_http(const char*);
extern uint16_t varnishd_client_port;
static int cfd = 0;
#define REUSE_CONNECTION

void http_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http(NULL);
        printf("Varnishd client port: %u\n", varnishd_client_port);
    }


    const struct sockaddr_in sin_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(varnishd_client_port),
        .sin_addr   = { .s_addr = 0x0100007F }
    };
    if (len == 0) return;

    if (cfd <= 0)
    {
        cfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (cfd < 0) {
            fprintf(stderr, "Could not create socket... going to sleep\n");
            sleep(1);
            return;
        }
        int flag = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));

        int ret = connect(cfd, (struct sockaddr*) &sin_addr, sizeof(sin_addr));
        if (ret < 0) {
            close(cfd);
            cfd = 0;
            fprintf(stderr, "Fuzzer connect() failed!?\n");
            sleep(1);
            return;
        }
    }

    const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1";
    int ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        //printf("Writing the request failed\n");
        close(cfd);
        cfd = 0;
        return;
    }

    for (size_t i = 0; i < len; i++) {
        ret = write(cfd, &data[i], 1);
        if (ret < 0) {
            //printf("Writing single bytes failed (idx=%zu/%zu)\n", i, len);
            close(cfd);
            cfd = 0;
            return;
        }
    }

#ifndef REUSE_CONNECTION
    // signalling end of request, increases exec/s by 4x
    shutdown(cfd, SHUT_WR);
#endif

    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
        close(cfd);
        cfd = 0;
        return;
    }
    else if (rlen == 0) {
        close(cfd);
        cfd = 0;
        return;
    }

#ifndef REUSE_CONNECTION
    close(cfd);
    cfd = 0;
#endif
}
