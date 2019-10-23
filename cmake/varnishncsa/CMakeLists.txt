set (PATH ${VARNISH_PATH}/bin/varnishncsa)
set(SOURCES
		${PATH}/varnishncsa.c
		${PATH}/b64.c
	)
add_executable(varnishncsa ${SOURCES})
target_include_directories(varnishncsa PUBLIC ${PATH})
target_include_directories(varnishncsa PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishncsa PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishncsa PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishncsa vapi_static)
# build varnishncsa into the top binary folder
set_target_properties(varnishncsa
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishncsa PUBLIC "${SANITIZE_FLAGS}")
endif()
