cmake_minimum_required (VERSION 3.0.2)
project(varnishstat C)

set (PATH ${VARNISH_PATH}/bin/varnishstat)
set(SOURCES
		${PATH}/varnishstat.c
		${PATH}/varnishstat_curses.c
	)
if (NOT VARNISH_PLUS)
	# enough incantations for do generate my help_gen
	add_executable(vstat_help_gen ${PATH}/varnishstat_help_gen.c)
	target_include_directories(vstat_help_gen PRIVATE ${VARNISH_PATH}/include)
	target_link_libraries(vstat_help_gen vapi_static)

	add_custom_command(
	    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/vstat_help_gen > vstat_gen.c
	    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	    OUTPUT  vstat_gen.c
	    DEPENDS vstat_help_gen ${PATH}/varnishstat_bindings.h
	    COMMENT "Generating vstat_gen.c"
	)
	add_custom_target(vstat_generate
		DEPENDS vstat_gen.c)

	list(APPEND SOURCES vstat_gen.c)
endif()

add_executable(varnishstat ${SOURCES})
target_include_directories(varnishstat PUBLIC ${PATH})
target_include_directories(varnishstat PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishstat PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishstat PUBLIC _XOPEN_SOURCE=700)
target_link_libraries(varnishstat vapi_static ncurses)
if (NOT VARNISH_PLUS)
	add_dependencies(varnishstat vstat_generate)
endif()
# build varnishstat into the top binary folder
set_target_properties(varnishstat
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
	target_compile_options(varnishstat PUBLIC "${SANITIZE_FLAGS}")
endif()
