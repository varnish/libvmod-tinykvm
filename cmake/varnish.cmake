# settings
option(VARNISH_PLUS "Build with Varnish plus" OFF)
set(VARNISH_DIR "/opt/varnish" CACHE STRING "Varnish installation")

# compiler flags
set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -g -O2")

if (VARNISH_PLUS)
	set(VMODTOOL "${VARNISH_DIR}/share/varnish-plus/vmodtool.py")
	set(VINCLUDE "${VARNISH_DIR}/include/varnish-plus")
	set(VLIBAPI  "${VARNISH_DIR}/lib/libvarnishapi.so")
else()
	set(VMODTOOL "${VARNISH_DIR}/share/varnish/vmodtool.py")
	set(VINCLUDE "${VARNISH_DIR}/include/varnish")
	set(VLIBAPI  "${VARNISH_DIR}/lib/libvarnishapi.so")
endif()

# enable make test
enable_testing()

add_library(varnishapi SHARED IMPORTED)
set_target_properties(varnishapi PROPERTIES IMPORTED_LOCATION "${VLIBAPI}")

function(add_vmod LIBNAME VCCNAME comment)
	# write empty config.h for autocrap
	file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.h
		"#define HAVE_OBJITERATE_F\n"
	)
	# generate VCC .c and .h
	get_filename_component(BASENAME ${VCCNAME} NAME_WE)
	set(VCCFILE  ${CMAKE_CURRENT_SOURCE_DIR}/${VCCNAME})
	set(OUTFILES ${BASENAME}_if.c ${BASENAME}_if.h)
	add_custom_command(
		COMMAND ${PYTHON_EXECUTABLE} ${VMODTOOL} --strict --boilerplate -o ${BASENAME}_if ${VCCFILE}
		DEPENDS ${VCCFILE}
		OUTPUT  ${OUTFILES}
	)
	# create VMOD shared object
	add_library(${LIBNAME} SHARED ${ARGN} ${OUTFILES})
	message(STATUS "Varnish include dir: ${VARNISH_INCLUDE_DIRS}")
	target_include_directories(${LIBNAME} PUBLIC ${VINCLUDE})
	target_include_directories(${LIBNAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(${LIBNAME} varnishapi)
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H)
	# this will build the final .so files in the top build directory
	# you don't always want this, so it can probably be an option,
	# but in most cases this will be the right thing to do
	set_target_properties(${LIBNAME}
    	PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
endfunction()


function(vmod_add_tests LIBNAME IMPORT_NAME)
	set(LIBPATH "${CMAKE_BINARY_DIR}/lib${LIBNAME}.so")
	add_test(NAME ${LIBNAME}_tests
		COMMAND varnishtest "-DVMOD_SO=\"${LIBPATH}\"" ${ARGN}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
endfunction()
