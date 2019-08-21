#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>


static const uint16_t client_port = 8080;
static char payload[4096];
static long payload_len = 0;

static long load_file(const char* filename)
{
    FILE *f = fopen(filename, "rb");
    assert(f);

    fseek(f, 0, SEEK_END);
    payload_len = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

    fread(payload, 1, payload_len, f);
    fclose(f);
    return payload_len;
}

int main()
{
    const struct sockaddr_in sin_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(client_port),
        .sin_addr   = { .s_addr = 0x0100007F }
    };

    const int cfd =
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket...\n");
        return 0;
    }
    int ret = connect(cfd, (struct sockaddr*) &sin_addr, sizeof(sin_addr));
    if (ret < 0) {
        close(cfd);
        fprintf(stderr, "Fuzzer connect() failed!?\n");
        return 0;
    }


    long plen = load_file("invalid1.req");
    ret = write(cfd, payload, plen);
    if (ret < 0) {
        close(cfd);
        return 0;
    }

    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
    }

    close(cfd);
    return 0;
}
