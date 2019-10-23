set (VH_PATH ${VARNISH_PATH}/bin/varnishhist)
set(VHIST_SOURCES
		${VH_PATH}/varnishhist.c
	)
add_executable(varnishhist ${VHIST_SOURCES})
target_include_directories(varnishhist PUBLIC ${VH_PATH})
target_include_directories(varnishhist PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishhist PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishhist PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishhist vapi_static ncurses pthread)
# build varnishhist into the top binary folder
set_target_properties(varnishhist
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishhist PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnishhist "${SANITIZE_FLAGS}")
endif()
