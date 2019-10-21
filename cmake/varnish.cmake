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
	set(VMODTOOL "${VARNISH_DIR}/share/vmodtool.py")
	set(VINCLUDE "${VARNISH_DIR}/include")
	set(VLIBAPI  "${VARNISH_DIR}/lib/libvarnishapi.so")
endif()

# enable make test
enable_testing()

add_library(varnishapi SHARED IMPORTED)
set_target_properties(varnishapi PROPERTIES IMPORTED_LOCATION "${VLIBAPI}")

function(add_vmod LIBNAME VCCNAME comment)
	# write empty config.h for autocrap
	file(WRITE ${CMAKE_BINARY_DIR}/config.h "")
	# generate VCC .c and .h
	get_filename_component(BASENAME ${VCCNAME} NAME_WE)
	set(VCCFILE  ${CMAKE_SOURCE_DIR}/${VCCNAME})
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
	target_include_directories(${LIBNAME} PUBLIC ${CMAKE_BINARY_DIR})
	target_link_libraries(${LIBNAME} varnishapi)
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H)
endfunction()


function(vmod_add_tests LIBNAME IMPORT_NAME)
	set(LIBPATH "${CMAKE_BINARY_DIR}/lib${LIBNAME}.so")
	add_test(NAME tests
		COMMAND varnishtest "-D${LIBNAME}=${IMPORT_NAME} from \"${LIBPATH}\"" ${ARGN}
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	)
endfunction()
