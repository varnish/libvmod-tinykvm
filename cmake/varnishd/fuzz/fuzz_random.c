#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_initialize(const char*);
extern int  open_varnishd_connection();

void random_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(NULL);
    }
	if (len == 0) return;

    int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

	ssize_t bytes = write(cfd, data, len);
    if (bytes < 0) {
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
