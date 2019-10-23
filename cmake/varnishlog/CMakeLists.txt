cmake_minimum_required (VERSION 3.0.2)
project(varnishlog C)

set (PATH ${VARNISH_PATH}/bin/varnishlog)
set(SOURCES
		${PATH}/varnishlog.c
	)
add_executable(varnishlog ${SOURCES})
target_include_directories(varnishlog PUBLIC ${PATH})
target_include_directories(varnishlog PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishlog PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(varnishlog PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishlog PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishlog varnishapi)
# build varnishlog into the top binary folder
set_target_properties(varnishlog
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_options(varnishlog PUBLIC "${COMMON_FLAGS}")
target_link_libraries(varnishlog "${COMMON_FLAGS}")
