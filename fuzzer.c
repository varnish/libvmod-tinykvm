#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static void*  fuzzer_data = NULL;
static size_t fuzzer_len  = 0;
static void fuzzer_begin(void* data, size_t len);

int LLVMFuzzerTestOneInput(void* data, size_t len)
{
    fuzzer_data = data;
    fuzzer_len  = len;

    static bool init = false;
    if (init == false) {
        init = true;
        const char* args[] = {
            "varnishd", "-a", ":8080",
            "-f", "/home/gonzo/github/varnish_autoperf/esi.vcl",
            "-F",
            "-n", "/tmp/varnish"
        };
        const int argc = sizeof(args) / sizeof(args[0]);
        normal_main(argc, args);
    }

    fuzzer_begin(data, len);
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

void fuzzer_begin(void* data, size_t len)
{
    const struct in_addr addr;
    if (inet_aton("127.0.0.1", &addr) == 0) abort();
    const struct sockaddr_in sin_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(8080),
        .sin_addr   = addr
    };
    const struct addrinfo res = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_addr = (struct sockaddr*) &sin_addr,
        .ai_addrlen = 4
    };
    printf("Fuzzer running corpus len = %zu\n", len);
    if (len == 0) return;

    const int cfd =
        socket(res.ai_family, res.ai_socktype, res.ai_protocol);
    if (cfd < 0) {
        abort();
    }
    const int ret = connect(cfd, res.ai_addr, res.ai_addrlen);
    if (ret < 0) {
        fprintf(stderr, "Fuzzer connect() failed\n");
        abort();
    }

    write(cfd, data, len);

    close(cfd);
}
