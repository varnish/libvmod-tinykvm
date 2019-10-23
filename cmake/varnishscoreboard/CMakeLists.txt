cmake_minimum_required (VERSION 3.0.2)
project(varnishscoreboard C)

set (PATH ${VARNISH_PATH}/bin/varnishscoreboard)
set(SOURCES
		${PATH}/varnishscoreboard.c
	)
add_executable(varnishscoreboard ${SOURCES})
target_include_directories(varnishscoreboard PUBLIC ${PATH})
target_include_directories(varnishscoreboard PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishscoreboard PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishscoreboard PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishscoreboard varnishapi)
# build varnishscoreboard into the top binary folder
set_target_properties(varnishscoreboard
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishscoreboard PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnishscoreboard "${SANITIZE_FLAGS}")
endif()
