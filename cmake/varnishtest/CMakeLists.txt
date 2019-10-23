cmake_minimum_required (VERSION 3.0.2)
project(varnishtest C)

set (PATH ${VARNISH_PATH}/bin/varnishtest)
set(SOURCES
		${PATH}/teken.c
		${PATH}/vtc_client.c
		${PATH}/vtc_haproxy.c
		${PATH}/vtc_log.c
		${PATH}/vtc_misc.c
		${PATH}/vtc_server.c
		${PATH}/vtc_varnish.c
		${PATH}/vtc_barrier.c
		${PATH}/vtc_h2_hpack.c
		${PATH}/vtc_http2.c
		${PATH}/vtc_logexp.c
		${PATH}/vtc_process.c
		${PATH}/vtc_subr.c
		${PATH}/vtc.c
		${PATH}/vtc_h2_tbl.c
		${PATH}/vtc_http.c
		${PATH}/vtc_main.c
		${PATH}/vtc_proxy.c
		${PATH}/vtc_syslog.c
	)
if (NOT VARNISH_PLUS)
	set(SOURCES ${SOURCES}
		${PATH}/vtc_gzip.c
	)
endif()
add_executable(varnishtest ${SOURCES})
target_include_directories(varnishtest PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishtest PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishtest PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishtest varnish pthread pcre vgz m)
# build varnishtest into the top binary folder
set_target_properties(varnishtest
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_options(varnishtest PUBLIC "${COMMON_FLAGS}")
target_link_libraries(varnishtest "${COMMON_FLAGS}")
