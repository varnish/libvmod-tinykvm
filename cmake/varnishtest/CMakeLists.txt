cmake_minimum_required (VERSION 3.0.2)
project(varnishtest C)

set (PATH ${VARNISH_PATH}/bin/varnishtest)

add_custom_command(
	COMMAND awk -f ${PATH}/gensequences ${PATH}/sequences > teken_state.h
	OUTPUT  teken_state.h
	DEPENDS ${PATH}/sequences ${PATH}/gensequences
	COMMENT "Generating teken_state header for varnishtest"
)
add_custom_target(teken_generate
	DEPENDS teken_state.h
)

add_custom_command(
	COMMAND ${Python3_EXECUTABLE} ${PATH}/huffman_gen.py ${VARNISH_PATH}/include/tbl/vhp_huffman.h > vtc_h2_dectbl.h
	OUTPUT  vtc_h2_dectbl.h
	DEPENDS ${VARNISH_PATH}/include/tbl/vhp_huffman.h
	COMMENT "Generating vtc_h2_dectbl header for varnishtest"
)
add_custom_target(vtc_h2_dectbl_generate
	DEPENDS vtc_h2_dectbl.h
)

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
		${PATH}/vtc_sess.c
	)
else()
	set(SOURCES ${SOURCES}
		${PATH}/vtc_dns.c
		${PATH}/vtc_file.c
	)
endif()
add_executable(varnishtest ${SOURCES})
add_dependencies(varnishtest teken_generate vtc_h2_dectbl_generate)
target_include_directories(varnishtest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(varnishtest PUBLIC VTEST_WITH_VTC_VARNISH=1)
target_compile_definitions(varnishtest PUBLIC VTEST_WITH_VTC_LOGEXPECT=1)

target_link_libraries(varnishtest vapi_static varnish resolv pthread vgz)
if (VARNISH_PLUS)
	target_link_libraries(varnishtest mse)
endif()
# build varnishtest into the top binary folder
set_target_properties(varnishtest
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_options(varnishtest PUBLIC "${COMMON_FLAGS}")
target_link_libraries(varnishtest "${COMMON_FLAGS}")

if (SANITIZE)
	target_compile_options(varnishtest PRIVATE "${SANITIZE_FLAGS}")
endif()
