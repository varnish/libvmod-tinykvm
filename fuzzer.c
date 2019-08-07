#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 1. connect using TCP socket and send requests
// 2. generate files for ESI parser
// 3. VCC fuzzing

static void http_fuzzer(void* data, size_t len);
extern int  normal_main(int, const char*[]);

int LLVMFuzzerTestOneInput(void* data, size_t len)
{

    http_fuzzer(data, len);
    return 0;
}

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

static uint16_t this_port;

static void varnishd_http()
{
    // clock_step is modified by mgt_main, so make it writable
    char cs_buffer[64];
    snprintf(cs_buffer, sizeof(cs_buffer), "clock_step=99999");
    // timeout idle is modified by mgt_main, make it writable
    char ti_buffer[64];
    snprintf(ti_buffer, sizeof(ti_buffer), "timeout_idle=0.01");
    // temp folder
    char vd_folder[128];
    snprintf(vd_folder, sizeof(vd_folder), "/tmp/varnish%d", getpid());
    // portline
    this_port = 8000 + (getpid() & 0xFFF);
    char portline[64];
    snprintf(portline, sizeof(portline), ":%u", this_port);
    // arguments to varnishd
    const char* args[] = {
        "varnishd", "-a", portline,
        //"-f", "/home/gonzo/github/varnish_autoperf/esi.vcl",
        "-b", ":8081",
        "-F",
        "-n", vd_folder,
        "-p", ti_buffer,
        "-p", cs_buffer // needed?
    };
    const int argc = sizeof(args) / sizeof(args[0]);
    normal_main(argc, args);
}

void http_fuzzer(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http();
    }
    const struct sockaddr_in sin_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(this_port),
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

    char buffer[2048];
    ssize_t rlen = read(cfd, buffer, sizeof(buffer));
    if (rlen < 0) {
        printf("Read failed\n");
    }

    close(cfd);
}
