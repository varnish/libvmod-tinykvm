cmake_minimum_required (VERSION 3.0.2)
project(varnishtest C)

set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -g -O2")

set (VT_PATH ${VARNISH_PATH}/bin/varnishtest)
set(VTEST_SOURCES
		${VT_PATH}/teken.c
		${VT_PATH}/vtc_client.c
		${VT_PATH}/vtc_haproxy.c
		${VT_PATH}/vtc_log.c
		${VT_PATH}/vtc_misc.c
		${VT_PATH}/vtc_server.c
		${VT_PATH}/vtc_varnish.c
		${VT_PATH}/vtc_barrier.c
		${VT_PATH}/vtc_h2_hpack.c
		${VT_PATH}/vtc_http2.c
		${VT_PATH}/vtc_logexp.c
		${VT_PATH}/vtc_process.c
		${VT_PATH}/vtc_subr.c
		${VT_PATH}/vtc.c
		${VT_PATH}/vtc_h2_tbl.c
		${VT_PATH}/vtc_http.c
		${VT_PATH}/vtc_main.c
		${VT_PATH}/vtc_proxy.c
		${VT_PATH}/vtc_syslog.c
	)
add_executable(varnishtest ${VTEST_SOURCES})
target_include_directories(varnishtest PUBLIC ${VARNISH_PATH}/bin/varnishd)
target_include_directories(varnishtest PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishtest PUBLIC ${VARNISH_PATH}/lib/libvgz)
target_include_directories(varnishtest PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(varnishtest PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishtest PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishtest varnish pthread pcre vgz m)
# build varnishtest into the top binary folder
set_target_properties(varnishtest
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishtest PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnishtest "${SANITIZE_FLAGS}")
endif()
