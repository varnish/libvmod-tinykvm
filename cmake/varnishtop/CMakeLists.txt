set (PATH ${VARNISH_PATH}/bin/varnishtop)
set(SOURCES
		${PATH}/varnishtop.c
	)
add_executable(varnishtop ${SOURCES})
target_include_directories(varnishtop PUBLIC ${PATH})
target_include_directories(varnishtop PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishtop PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishtop PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishtop vapi_static ncurses pthread)
# build varnishtop into the top binary folder
set_target_properties(varnishtop
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishtop PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnishtop "${SANITIZE_FLAGS}")
endif()
