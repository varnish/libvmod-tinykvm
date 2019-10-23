cmake_minimum_required (VERSION 3.0.2)
project(varnishstat C)

set (PATH ${VARNISH_PATH}/bin/varnishstat)
set(SOURCES
		${PATH}/varnishstat.c
		${PATH}/varnishstat_curses.c
	)
add_executable(varnishstat ${SOURCES})
target_include_directories(varnishstat PUBLIC ${PATH})
target_include_directories(varnishstat PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishstat PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishstat PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishstat vapi_static ncurses)
# build varnishstat into the top binary folder
set_target_properties(varnishstat
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishstat PUBLIC "${SANITIZE_FLAGS}")
endif()
