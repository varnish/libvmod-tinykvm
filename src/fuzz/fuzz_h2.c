#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_http();
extern uint16_t varnishd_client_port;

#define H2F_HEADERS    0x1
#define H2F_SETTINGS   0x4


struct h2_frame
{
	uint32_t length  : 24;
	uint32_t type    :  8;
	uint8_t  flags;
	uint32_t stream_id;
	// FMA
	char payload[0];
};
struct h2_headers
{
	struct h2_frame frame;
	uint32_t stream_dep;
	uint32_t block_frag[0];
};

void h2_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http();
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


    const char* req = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        close(cfd);
        return;
    }

	const uint8_t* buffer = (uint8_t*) data;
    // settings frame
	const int slen = 6; // settings are of 6-byte multiple length
	struct h2_frame settings;
	settings.type   = H2F_SETTINGS;
	settings.length = __builtin_bswap32(slen << 8);
	settings.flags  = buffer[0];
	settings.stream_id = 0;
	buffer++;
	len--;
	// settings frame
	ret = write(cfd, &settings, sizeof(settings));
	if (ret < 0) {
		close(cfd);
		return;
	}

	if (len >= 6 + 2)
	{
		// settings payload
		ret = write(cfd, buffer, slen);
	    if (ret < 0) {
	        close(cfd);
	        return;
	    }
		buffer += slen;
		len    -= slen;

		struct h2_headers hdr;
		hdr.frame.type   = H2F_HEADERS;
		hdr.frame.flags  = buffer[0]; // END_HEADERS ?
		hdr.frame.stream_id = buffer[1];
		hdr.stream_dep = 0;
		buffer += 2;
		len    -= 2;
		hdr.frame.length = __builtin_bswap32(len << 8);

		ret = write(cfd, &hdr, sizeof(hdr));
	    if (ret < 0) {
	        close(cfd);
	        return;
	    }
	}

    ret = write(cfd, buffer, len);
    if (ret < 0) {
        close(cfd);
        return;
    }
    // signalling end of request, increases exec/s by 7x
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
