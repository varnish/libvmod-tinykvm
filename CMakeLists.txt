cmake_minimum_required(VERSION 3.10)
project(compute C CXX)

option(VARNISH_PLUS "Build with Varnish plus" ON)
# Set the C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)

if (VARNISH_PLUS)
	set(VTOOLDIR   "/usr/share/varnish-plus")
	set(VSCTOOLDIR "/usr/share/varnish-plus")
	set(VINCLUDE   "${VARNISH_SOURCE_DIR}/include")
else()
	set(VTOOLDIR   "/usr/share/varnish")
	set(VSCTOOLDIR "/usr/share/varnish")
	set(VINCLUDE   "/usr/include/varnish")
endif()

set(VMODTOOL "${VTOOLDIR}/vmodtool.py")
set(VSCTOOL  "${VSCTOOLDIR}/vsctool.py")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVARNISH REQUIRED IMPORTED_TARGET varnishapi)
find_program(VARNISHD    "varnishd")
find_program(VARNISHTEST "varnishtest")

# Example: vmod_debug vmod_debug.vcc
function(add_vmod LIBNAME VCCNAME comment)
	# generate config.h
	configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/config.h)
	# generate VCC .c and .h
	set(VCCFILE "${CMAKE_CURRENT_SOURCE_DIR}/${VCCNAME}")
	set(OUTFILES vcc_if.c vcc_if.h)
	# for compatibility with automake naming
	# NOTE: this will *NOT* work on Windows
	string(REPLACE "vmod_" "" SILLY_NAME ${LIBNAME})
	set(SILLY_NAME "vcc_${SILLY_NAME}_if")
	add_custom_command(
		COMMAND ${Python3_EXECUTABLE} ${VMODTOOL} -o vcc_if ${VCCFILE}
		COMMAND ${CMAKE_COMMAND} -E create_symlink vcc_if.c ${SILLY_NAME}.c
		COMMAND ${CMAKE_COMMAND} -E create_symlink vcc_if.h ${SILLY_NAME}.h
		DEPENDS ${VCCFILE} ${VMODTOOL}
		OUTPUT  ${OUTFILES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	# create VMOD shared object
	add_library(${LIBNAME} SHARED ${ARGN} ${OUTFILES})
	target_include_directories(${LIBNAME} PRIVATE ${CMAKE_BINARY_DIR})
	target_include_directories(${LIBNAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	target_include_directories(${LIBNAME} PUBLIC ${VINCLUDE})
	#target_compile_options(${LIBNAME} PRIVATE "-fno-lto") # LTO discards too much
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H _GNU_SOURCE)
	if (VARNISH_PLUS)
		target_compile_definitions(${LIBNAME} PRIVATE VARNISH_PLUS=1)
		target_link_libraries(${LIBNAME} PkgConfig::LIBVARNISH)
	else()
		if (EXISTS "/usr/lib/varnish/libvarnishapi.so.2")
			target_link_libraries(${LIBNAME} -l:libvarnishapi.so.2)
		else()
			target_link_libraries(${LIBNAME} PkgConfig::LIBVARNISH)
		endif()
	endif()
	if (SANITIZE)
		target_compile_options(${LIBNAME} PRIVATE
			"-fno-omit-frame-pointer" "-fsanitize=address,undefined" "-fno-sanitize=function,vptr")
		target_compile_definitions(${LIBNAME} PRIVATE SANITIZERS_ENABLED=1)
		target_link_libraries(${LIBNAME} "-fsanitize=address,undefined")
	endif()
	# this will build the final .so files in the top build directory
	# you don't always want this, so it can probably be an option,
	# but in most cases this will be the right thing to do
	set_target_properties(${LIBNAME}
		PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
endfunction()

function(add_vmod_vsc LIBNAME VSCNAME)
	# generate VSC .c and .h
	get_filename_component(BASENAME ${VSCNAME} NAME_WE)
	set(VSCFILE  ${CMAKE_CURRENT_SOURCE_DIR}/${VSCNAME})
	set(OUTFILES ${BASENAME}.c ${BASENAME}.h)
	add_custom_command(
		COMMAND ${Python3_EXECUTABLE} ${VSCTOOL} -ch ${VSCFILE}
		DEPENDS ${VSCFILE}
		OUTPUT  ${OUTFILES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	set_source_files_properties(${OUTFILES} PROPERTIES GENERATED TRUE)
	target_sources(${LIBNAME} PRIVATE ${OUTFILES})
endfunction()

function(add_vmod_tests LIBNAME IMPORT_NAME)
	if (NOT SINGLE_PROCESS)
		set(LIBPATH "${CMAKE_BINARY_DIR}/lib${LIBNAME}.so")
		set(VMOD_PATH "${CMAKE_BINARY_DIR}")
		# varnishtest doesn't like to run with no tests
		foreach (FILENAME ${ARGN})
		get_filename_component(TEST ${FILENAME} NAME_WE)
		add_test(NAME ${LIBNAME}_${TEST}
			COMMAND ${VARNISHTEST} -l "-Dvarnishd=${VARNISHD}" "-DVMOD_SO=\"${LIBPATH}\"" "-Dtestname=${TEST}" "-Dtopsrc=${VARNISH_SOURCE_DIR}" "-pvmod_path=${VMOD_PATH}" ${FILENAME}
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		)
		set_tests_properties(${LIBNAME}_${TEST}
			PROPERTIES  ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}:$ENV{PATH}"
						TIMEOUT 120
						SKIP_RETURN_CODE 77
		)
		endforeach()
	else()
		# warn about SINGLE_PROCESS being incompatible with tests?
	endif()
endfunction()

add_subdirectory(lib/libkvm libkvm)

enable_testing()

add_subdirectory(src)
