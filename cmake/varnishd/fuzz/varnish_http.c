#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

extern int  varnishd_main(int, const char*[]);
uint16_t varnishd_client_port;
const char* varnishd_client_path = NULL;
bool varnishd_proxy_mode = false;
int  varnishd_threadpool_size = 8;

void varnishd_initialize(const char* vcl_path)
{
	// if proxy is enabled, add ,proxy to the -a option:
	const char* portopts = 
		(varnishd_proxy_mode) ? "%s,proxy" : "%s";
	
	// create unix-domain socket for listener port
	char* client_path = malloc(128);
	snprintf(client_path, 128, "/tmp/varnishd_%u.sock", getpid());
	varnishd_client_path = client_path;
    // clock_step is modified by mgt_main, so make it writable
    char cs_buffer[64];
    snprintf(cs_buffer, sizeof(cs_buffer), "clock_step=99999");
    // timeout idle is modified by mgt_main, make it writable
    char ti_buffer[64];
    snprintf(ti_buffer, sizeof(ti_buffer), "timeout_idle=0.001");
	// threadpool min buffer
    char tpmin_buffer[128];
    snprintf(tpmin_buffer, sizeof(ti_buffer), 
			"thread_pool_min=%d", varnishd_threadpool_size);
	// threadpool max buffer
    char tpmax_buffer[128];
    snprintf(tpmax_buffer, sizeof(ti_buffer), 
			"thread_pool_max=%d", varnishd_threadpool_size);
    // temp folder
    char vd_folder[128];
    snprintf(vd_folder, sizeof(vd_folder), "/tmp/varnishd_%d", getpid());
	// feature http2
	char feature_http2[128];
	snprintf(feature_http2, sizeof(feature_http2), "feature=+http2");
	// debug flag single-process
	char feature_siproc[128];
#ifndef VARNISH_PLUS
	snprintf(feature_siproc, sizeof(feature_siproc), "debug=+execute_mode");
#else
	snprintf(feature_siproc, sizeof(feature_siproc), "debug=none");
#endif
    // client and cli ports
    char portline[128];
    snprintf(portline, sizeof(portline), portopts, varnishd_client_path);
    const uint16_t cli_port = 25000 + (getpid() & 0x3FFF);
    char cli_portline[64];
    snprintf(cli_portline, sizeof(cli_portline), ":%u", cli_port);
    // arguments to varnishd
    const char* args[] = {
		"varnishd", "-a", portline,
		"-T", cli_portline,
		"-F",
		"-n", vd_folder,
		"-p", feature_http2,
		"-p", feature_siproc,
		"-p", ti_buffer,
		"-p", cs_buffer, // needed?
		"-p", tpmin_buffer,
		"-p", tpmax_buffer,
		// -b must be last (see below)
		"-b", ":8081",
    };

	const int argc = sizeof(args) / sizeof(args[0]);
	// replace backend with VCL when needed
    if (vcl_path != NULL) {
        args[argc-2] = "-f";
        args[argc-1] = vcl_path;
    }
	for (int i = 0; i < argc; i++) {
		printf("%s ", args[i]);
	}
	printf("\n");
    varnishd_main(argc, args);
}
