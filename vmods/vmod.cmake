# settings
option(LIBFUZZER    "Build for libfuzzer" OFF)
option(VARNISH_PLUS "Build with Varnish plus" ON)
option(VMOD_USE_LOCAL_VC "Build with local Varnish" ON)
set(VARNISH_DIR "/opt/varnish" CACHE STRING "Varnish installation")
# this needs to be set to build the std vmod:
set(VARNISH_SOURCE_DIR "" CACHE STRING "Varnish source directory")

# compiler flags only when building alone
if (NOT TARGET varnishd)
	# NOTE: varnish uses non-standard features so use GNU
	set(CMAKE_C_FLAGS "-Wall -Wextra -std=gnu11 -g -O2")
endif()

# complete the source path if it wasn't absolute
if (NOT IS_ABSOLUTE ${VARNISH_SOURCE_DIR})
	set(VARNISH_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${VARNISH_SOURCE_DIR})
endif()

if (VARNISH_PLUS)
	if (VMOD_USE_LOCAL_VC)
		set(VTOOLDIR "${VARNISH_SOURCE_DIR}/lib/libvcc")
		set(VINCLUDE "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VTOOLDIR "${VARNISH_SOURCE_DIR}/share/varnish-plus")
		set(VINCLUDE "${VARNISH_DIR}/include/varnish-plus")
	endif()
else()
	if (VMOD_USE_LOCAL_VC)
		set(VTOOLDIR "${VARNISH_SOURCE_DIR}/lib/libvcc")
		set(VINCLUDE "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VTOOLDIR "${VARNISH_DIR}/share/varnish")
		set(VINCLUDE "${VARNISH_DIR}/include/varnish")
	endif()
endif()

if (TARGET varnishd)
	set(VARNISHD    ${CMAKE_BINARY_DIR}/varnishd)
	set(VARNISHTEST ${CMAKE_BINARY_DIR}/varnishtest)
elseif (VMOD_USE_LOCAL_VC)
	set(VARNISHD    ${VARNISH_SOURCE_DIR}/bin/varnishd/varnishd)
	set(VARNISHTEST ${VARNISH_SOURCE_DIR}/bin/varnishtest/varnishtest)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBVARNISH REQUIRED IMPORTED_TARGET varnishapi)
	find_program(VARNISHD    "varnishd")
	find_program(VARNISHTEST "varnishtest")
endif()
# this will fill the PYTHON_EXECUTABLE variable, which is only
# required when trying to run a python script without the executable bit
find_package(Python3 COMPONENTS Interpreter)

set(VMODTOOL "${VTOOLDIR}/vmodtool.py")
set(VSCTOOL  "${VTOOLDIR}/vsctool.py")

find_package(Threads)

# Example: vmod_debug vmod_debug.vcc
function(add_vmod LIBNAME VCCNAME comment)
	# write empty config.h for autocrap
	file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.h
		"#define HAVE_OBJITERATE_F\n"
		"#define HAVE_GETPEEREID 1\n"
		"#define HAVE_SETPPRIV 1\n"
	)
	# generate VCC .c and .h
	if (EXISTS "${VCCNAME}" OR IS_ABSOLUTE "${VCCNAME}")
		set(VCCFILE "${VCCNAME}")
	else() # try relative to source directory
		set(VCCFILE "${CMAKE_CURRENT_SOURCE_DIR}/${VCCNAME}")
	endif()
	set(OUTFILES vcc_if.c vcc_if.h)
	# for compatibility with automake naming
	# NOTE: this will *NOT* work on Windows
	string(REPLACE "vmod_" "" SILLY_NAME ${LIBNAME})
	set(SILLY_NAME "vcc_${SILLY_NAME}_if")
	add_custom_command(
		COMMAND ${Python3_EXECUTABLE} ${VMODTOOL} -o vcc_if ${VCCFILE}
		COMMAND ${CMAKE_COMMAND} -E create_symlink vcc_if.c ${SILLY_NAME}.c
		COMMAND ${CMAKE_COMMAND} -E create_symlink vcc_if.h ${SILLY_NAME}.h
		DEPENDS ${VCCFILE}
		OUTPUT  ${OUTFILES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	# create VMOD shared object
	add_library(${LIBNAME} SHARED ${ARGN} ${OUTFILES})
	if (VMOD_USE_LOCAL_VC)
		if (TARGET varnishapi)
			add_dependencies(${LIBNAME} includes_generate)
			target_link_libraries(${LIBNAME} varnishapi)
		else()
			target_link_libraries(${LIBNAME} ${VARNISH_SOURCE_DIR}/lib/libvarnishapi/.libs/libvarnishapi.so)
		endif()
		target_include_directories(${LIBNAME} PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
	else()
		target_link_libraries(${LIBNAME} PkgConfig::LIBVARNISH)
	endif()
	target_include_directories(${LIBNAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	target_include_directories(${LIBNAME} PUBLIC ${VINCLUDE})
	target_link_libraries(${LIBNAME} Threads::Threads)
	#target_compile_options(${LIBNAME} PRIVATE "-fno-lto") # LTO discards too much
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H _GNU_SOURCE)
	target_compile_options(${LIBNAME} PRIVATE "-fdiagnostics-color")
	if (VARNISH_PLUS)
		target_compile_definitions(${LIBNAME} PRIVATE VARNISH_PLUS=1)
	endif()
	if (LIBFUZZER)
		target_compile_options(${LIBNAME} PRIVATE 
			"-fno-omit-frame-pointer" "-fsanitize=fuzzer-no-link,address,undefined")
		target_compile_definitions(${LIBNAME} PRIVATE LIBFUZZER_ENABLED=1)
		target_link_libraries(${LIBNAME} "-fsanitize=fuzzer,address,undefined")
		message(STATUS "Libfuzzer enabled for VMOD ${LIBNAME}")
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
	if (EXISTS "${VSCNAME}" OR EXISTS ${VSCNAME})
		set(VSCFILE ${VSCNAME})
	else() # try relative to source directory
		set(VSCFILE  ${CMAKE_CURRENT_SOURCE_DIR}/${VSCNAME})
	endif()
	set(OUTFILES ${BASENAME}.c ${BASENAME}.h)
	add_custom_command(
		COMMAND ${Python3_EXECUTABLE} ${VSCTOOL} -ch ${VSCFILE}
		DEPENDS ${VSCFILE}
		OUTPUT  ${OUTFILES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	target_sources(${LIBNAME} PRIVATE ${OUTFILES})
endfunction()

enable_testing()

function(add_vmod_tests LIBNAME IMPORT_NAME)
	if (NOT SINGLE_PROCESS)
		set(LIBPATH "${CMAKE_BINARY_DIR}/lib${LIBNAME}.so")
		set(VMOD_PATH "${CMAKE_BINARY_DIR}")
		# varnishtest doesn't like to run with no tests
		foreach (FILENAME ${ARGN})
		get_filename_component(TEST ${FILENAME} NAME_WE)
		add_test(NAME ${LIBNAME}_${TEST}
			COMMAND ${VARNISHTEST} "-Dvarnishd=${VARNISHD}" "-DVMOD_SO=\"${LIBPATH}\"" -p "vmod_path=${VMOD_PATH}" ${FILENAME}
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
