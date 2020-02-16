#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

extern void varnishd_initialize();
extern int  open_varnishd_connection();
static int cfd = -1;
static inline void close_varnishd_connection()
{
	close(cfd);
	cfd = -1;
}

#define static_assert _Static_assert
static const int FUZZ_H2_HEADERS = true;
#define H2F_HEADERS    0x1
#define H2F_SETTINGS   0x4

struct h2_frame
{
	uint32_t length  : 24;
	uint32_t type    :  8;
	uint8_t  flags;
	uint32_t stream_id;
	// settings are of 6-byte multiple length
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
	char payload[6];
};

void h2_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(NULL);
    }
	if (len == 0) return;

	while (cfd < 0) {
		cfd = open_varnishd_connection();
	}
	const uint8_t* buffer = (uint8_t*) data;

	const char* req = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
	ssize_t bytes = write(cfd, req, strlen(req));
	if (bytes < 0) {
		close_varnishd_connection();
		return;
	}

	if (false)
	{
	    // settings frame
		struct h2_settings settings;
		const int slen = sizeof(settings.payload);
		static_assert(sizeof(settings.payload) == 6, "Must be 6 bytes?");

		settings.frame.type   = H2F_SETTINGS;
		settings.frame.length = __builtin_bswap32(slen << 8);
		settings.frame.stream_id = 0;

		if (len >= slen)
		{
			memcpy(settings.payload, data, slen);
			data += slen;
			len  -= slen;
		}

		for (int i = 0; i < 10; i++)
		{
			// flags in settings frame
			if (len > 0) {
				settings.frame.flags  = buffer[0];
				buffer++;
				len--;
			}

			bytes = write(cfd, &settings, sizeof(settings));
			if (bytes < 0) {
				close_varnishd_connection();
				return;
			}
		}

		if (FUZZ_H2_HEADERS)
		{
			while (len >= 2)
			{
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
					close_varnishd_connection();
			        return;
			    }
			}
		}
	} // newly opened

    bytes = write(cfd, buffer, len);
    if (bytes < 0) {
		close_varnishd_connection();
        return;
    }
    // signalling end of request, increases exec/s by 7x
    //shutdown(cfd, SHUT_WR);

	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 10000
	};
	fd_set set;
	FD_ZERO(&set);
	FD_SET(cfd, &set);

	int s = select(cfd + 1, &set, NULL, NULL, &timeout);
	if (s == -1 || s == 0) {
		close_varnishd_connection();
		return;
	}

    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
		close_varnishd_connection();
    }
	//close(cfd);
}
