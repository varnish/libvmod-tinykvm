#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

extern void varnishd_http(const char* vcl_path);
extern uint16_t varnishd_client_port;
static const uint16_t BACKEND_PORT = 8081;

void http_fuzzer_server(void* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_http("/home/gonzo/github/varnish_autoperf/vcl/pass.vcl");
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


    const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    ret = write(cfd, req, strlen(req));
    if (ret < 0) {
        close(cfd);
        return;
    }
    // signalling end of request, increases exec/s by 4x
    shutdown(cfd, SHUT_WR);

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(BACKEND_PORT);
    srv_addr.sin_addr.s_addr = INADDR_ANY;

    int server = socket(AF_INET, SOCK_STREAM, 0);

    int enable = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    ret = bind(server, (struct sockaddr*) &srv_addr, sizeof(srv_addr));
    if (ret < 0) {
        fprintf(stderr, "Bind failed: %s\n", strerror(errno));
        goto fixup;
    }
    ret = listen(server, 1);
    if (ret < 0) {
        fprintf(stderr, "Listen failed: %s\n", strerror(errno));
        goto fixup;
    }

    // listen for backend clients
    struct sockaddr_in cli_addr;
    socklen_t addrlen = sizeof(cli_addr);
    int client = accept(server, (struct sockaddr*) &cli_addr, &addrlen);
    if (client < 0) {
        fprintf(stderr, "Accept failed: %s\n", strerror(errno));
        goto fixup;
    }

    // write bullshit to varnishd
    const char* resp = "HTTP/1.1 200 OK\r\nETag: ";
    ret = write(client, resp, strlen(resp));
    if (ret < 0) {
        printf("Backend write failed: %s\n", strerror(errno));
        close(client);
        goto fixup;
    }
    ret = write(client, data, len);
    if (ret < 0) {
        printf("Backend write failed: %s\n", strerror(errno));
    }
    close(client);

fixup:
    close(server);
    close(cfd);
}
