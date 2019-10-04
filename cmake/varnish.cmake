option(VARNISH_PLUS "Build with Varnish plus" OFF)

set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -g -O2")

set(VMODTOOL "/usr/local/share/varnish-plus/vmodtool.py" CACHE STRING "VMOD tool location")

find_package(PkgConfig)
pkg_check_modules(VARNISH REQUIRED varnishapi)

function(add_vmod LIBNAME VCCNAME comment)
	add_library(${LIBNAME} SHARED ${ARGN} ${VCC_C_FILES})
	target_include_directories(${LIBNAME} PUBLIC ${VARNISH_INCLUDE_DIRS})
	target_include_directories(${LIBNAME} PUBLIC ${CMAKE_BINARY_DIR})
	target_link_libraries(${LIBNAME} ${VARNISH_LIBRARIES})
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1)

	vmod_add_vcl(${LIBNAME} ${VCCNAME})
endfunction()

function(vmod_add_vcl LIBNAME)
	get_filename_component(BASENAME ${ARGN} NAME_WE)
	set(OUTFILES ${BASENAME}_if.c ${BASENAME}_if.h)
	add_custom_command(
		COMMAND ${PYTHON_EXECUTABLE} ${VMODTOOL} -o ${BASENAME}_if ${CMAKE_SOURCE_DIR}/${ARGN}
		DEPENDS ${INFILE}
		OUTPUT  ${OUTFILES}
	)
	add_custom_target(generated_vcc_sources
		DEPENDS ${OUTFILES}
	)
	add_dependencies(${LIBNAME} generated_vcc_sources)
	list(APPEND VCC_C_FILES ${OUTFILES} PARENT_SCOPE)
endfunction()

function(vmod_add_tests LIBNAME)
	add_test(NAME tests
		COMMAND varnishtest ${ARGN}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	)
endfunction()
