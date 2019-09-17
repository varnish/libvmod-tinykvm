#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern int  varnishd_main(int, const char*[]);
uint16_t varnishd_client_port;

void varnishd_http(const char* vcl_path)
{
    // clock_step is modified by mgt_main, so make it writable
    char cs_buffer[64];
    snprintf(cs_buffer, sizeof(cs_buffer), "clock_step=99999");
    // timeout idle is modified by mgt_main, make it writable
    char ti_buffer[64];
    snprintf(ti_buffer, sizeof(ti_buffer), "timeout_idle=0.001");
	// threadpool min buffer
    char tpmin_buffer[128];
    snprintf(tpmin_buffer, sizeof(ti_buffer), "thread_pool_min=32");
	// threadpool max buffer
    char tpmax_buffer[128];
    snprintf(tpmax_buffer, sizeof(ti_buffer), "thread_pool_max=32");
    // temp folder
    char vd_folder[128];
    snprintf(vd_folder, sizeof(vd_folder), "/tmp/varnish%d", getpid());
    // client and cli ports
    varnishd_client_port = 20000 + (getpid() & 0xFFF);
    char portline[64];
    snprintf(portline, sizeof(portline), ":%u", varnishd_client_port);
    const uint16_t cli_port = 25000 + (getpid() & 0xFFF);
    char cli_portline[64];
    snprintf(cli_portline, sizeof(cli_portline), ":%u", cli_port);
    // arguments to varnishd
    const char* args[] = {
        "varnishd", "-a", portline,
        "-T", cli_portline,
        "-F",
        "-n", vd_folder,
        "-p", ti_buffer,
        "-p", cs_buffer, // needed?
		"-p", tpmin_buffer,
		"-p", tpmax_buffer,
        "-b", ":8081",
    };
    if (vcl_path != NULL) {
        args[12] = "-f";
        args[13] = vcl_path;
    }
    const int argc = sizeof(args) / sizeof(args[0]);
    varnishd_main(argc, args);
}
