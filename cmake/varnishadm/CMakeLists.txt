cmake_minimum_required (VERSION 3.0.2)
project(varnishadm C)

set (PATH ${VARNISH_PATH}/bin/varnishadm)
set(SOURCES
		${PATH}/varnishadm.c
	)
add_executable(varnishadm ${SOURCES})
target_include_directories(varnishadm PUBLIC ${PATH})
target_include_directories(varnishadm PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishadm PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishadm PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishadm vapi_static edit)
# build varnishadm into the top binary folder
set_target_properties(varnishadm
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishadm PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnishadm "${SANITIZE_FLAGS}")
endif()
