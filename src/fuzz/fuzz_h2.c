#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

extern void varnishd_http();
extern int  open_varnishd_connection();

#define static_assert _Static_assert
static const int GENERAL_H2_FUZZING = true;
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
} __attribute__((packed));
static_assert(sizeof(struct h2_frame) == 9, "");

struct h2_headers
{
	struct h2_frame frame;
	uint32_t stream_dep;
	uint32_t block_frag[0];
};
struct h2_settings
{
	struct h2_frame frame;
	char payload[0];
};

void h2_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http(NULL);
    }
	if (len == 0) return;

	static int cfd = -1;
	while (cfd < 0) {
		cfd = open_varnishd_connection();
	}
	const uint8_t* buffer = (uint8_t*) data;

/*
	const char* req = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        close(cfd);
        return;
    }
*/
    // settings frame
	const int slen = 6; // settings are of 6-byte multiple length
	struct h2_frame settings;
/*
	settings.type   = H2F_SETTINGS;
	settings.length = __builtin_bswap32(slen << 8);
	settings.stream_id = 0;

	char settings_buffer[sizeof(settings) + slen];
	memcpy(&settings_buffer[0], &settings, sizeof(settings));
	if (len >= slen)
	{
		memcpy(&settings_buffer[sizeof(settings)], data, slen);
		data += slen;
		len  -= slen;
	}

	for (int i = 0; i < 0; i++)
	{
		// settings frame
		if (len > 0) {
			settings.flags  = buffer[0];
			buffer++;
			len--;
			memcpy(&settings_buffer[0], &settings, sizeof(settings));
		}

		ret = write(cfd, settings_buffer, sizeof(settings_buffer));
		if (ret < 0) {
			close(cfd);
			return;
		}
	}
*/

if (!GENERAL_H2_FUZZING)
{
	if (len >= 6 + 2)
	{
		// settings payload
		ssize_t bytes = write(cfd, buffer, slen);
	    if (bytes < 0) {
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

		bytes = write(cfd, &hdr, sizeof(hdr));
	    if (bytes < 0) {
	        close(cfd);
	        return;
	    }
	}
}

    ssize_t bytes = write(cfd, buffer, len);
    if (bytes < 0) {
        close(cfd);
		cfd = -1;
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
		close(cfd);
		cfd = -1;
    }

}
