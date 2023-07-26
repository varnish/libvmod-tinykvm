# settings
option(LIBFUZZER    "Build for libfuzzer" OFF)
option(VARNISH_PLUS "Build with Varnish plus" ON)
option(VMOD_RELEASE_BUILD "Build with package Varnish" OFF)
option(VMOD_USE_LOCAL_VC "Build with local Varnish" ON)
set(VARNISH_DIR "/opt/varnish" CACHE STRING "Varnish installation")
# this needs to be set to build the std vmod:
set(VARNISH_SOURCE_DIR "" CACHE STRING "Varnish source directory")

# complete the source path if it wasn't absolute
if (NOT IS_ABSOLUTE ${VARNISH_SOURCE_DIR})
	set(VARNISH_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${VARNISH_SOURCE_DIR})
endif()

if (VARNISH_PLUS)
	if (VMOD_RELEASE_BUILD)
		set(VTOOLDIR   "/usr/share/varnish-plus")
		set(VSCTOOLDIR "/usr/share/varnish-plus")
		set(VINCLUDE   "${VARNISH_SOURCE_DIR}/include")
	elseif (VMOD_USE_LOCAL_VC)
		set(VTOOLDIR   "${VARNISH_SOURCE_DIR}/lib/libvcc")
		set(VSCTOOLDIR "${VARNISH_SOURCE_DIR}/lib/libvsc")
		set(VINCLUDE   "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VTOOLDIR   "${VARNISH_SOURCE_DIR}/share/varnish-plus")
		set(VSCTOOLDIR "${VARNISH_SOURCE_DIR}/share/varnish-plus")
		set(VINCLUDE   "${VARNISH_DIR}/include/varnish-plus")
	endif()
else()
	if (VMOD_USE_LOCAL_VC)
		set(VTOOLDIR   "${VARNISH_SOURCE_DIR}/lib/libvcc")
		set(VSCTOOLDIR "${VARNISH_SOURCE_DIR}/lib/libvsc")
		set(VINCLUDE   "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VTOOLDIR   "${VARNISH_DIR}/share/varnish")
		set(VSCTOOLDIR "${VARNISH_DIR}/share/varnish")
		set(VINCLUDE   "${VARNISH_DIR}/include/varnish")
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

if (NOT Python3_EXECUTABLE)
# this will fill the Python3_EXECUTABLE variable, which is only
# required when trying to run a python script without the executable bit
find_package(Python3 COMPONENTS Interpreter)
endif()

set(VMODTOOL "${VTOOLDIR}/vmodtool.py")
set(VSCTOOL  "${VSCTOOLDIR}/vsctool.py")
set(VMOD_CMAKE_PATH ${CMAKE_CURRENT_LIST_DIR})

# Example: vmod_debug vmod_debug.vcc
function(add_vmod LIBNAME VCCNAME comment)
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
		DEPENDS ${VCCFILE} ${VMODTOOL}
		OUTPUT  ${OUTFILES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
	# create VMOD shared object
	add_library(${LIBNAME} SHARED ${ARGN} ${OUTFILES})
	target_include_directories(${LIBNAME} PUBLIC ${VMOD_CMAKE_PATH}/include)
	if (VMOD_RELEASE_BUILD)
		target_link_libraries(${LIBNAME} /usr/lib/libvarnishapi.so)
		target_include_directories(${LIBNAME} PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
		target_link_options(${LIBNAME} PUBLIC -Wl,-rpath="/usr/lib/varnish-plus")
	elseif (VMOD_USE_LOCAL_VC)
		set (INTREE_VAPI "${VARNISH_SOURCE_DIR}/lib/libvarnishapi/.libs/libvarnishapi.so")
		if (EXISTS ${INTREE_VAPI})
			target_link_libraries(${LIBNAME} ${INTREE_VAPI})
		else()
			target_link_libraries(${LIBNAME} varnishapi)
		endif()
		target_include_directories(${LIBNAME} PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
	else()
		if (VARNISH_PLUS)
			target_link_libraries(${LIBNAME} /usr/lib/varnish-plus/libvarnishapi.so)
		else()
			target_link_libraries(${LIBNAME} PkgConfig::LIBVARNISH)
		endif()
	endif()
	target_include_directories(${LIBNAME} PRIVATE ${CMAKE_BINARY_DIR})
	target_include_directories(${LIBNAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	target_include_directories(${LIBNAME} PUBLIC ${VINCLUDE})
	#target_compile_options(${LIBNAME} PRIVATE "-fno-lto") # LTO discards too much
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H _GNU_SOURCE)
	if (VARNISH_PLUS)
		target_compile_definitions(${LIBNAME} PRIVATE VARNISH_PLUS=1)
	endif()
	if (LIBFUZZER)
		target_compile_options(${LIBNAME} PRIVATE
			"-fno-omit-frame-pointer" "-fsanitize=fuzzer-no-link,address,undefined")
		target_compile_definitions(${LIBNAME} PRIVATE LIBFUZZER_ENABLED=1)
		target_link_libraries(${LIBNAME} "-fsanitize=fuzzer,address,undefined")
		message(STATUS "Libfuzzer enabled for VMOD ${LIBNAME}")
	elseif (SANITIZE)
		target_compile_options(${LIBNAME} PRIVATE
			"-fno-omit-frame-pointer" "-fsanitize=address,undefined" "-fno-sanitize=function,vptr")
		target_compile_definitions(${LIBNAME} PRIVATE SANITIZERS_ENABLED=1)
		target_link_libraries(${LIBNAME} "-fsanitize=address,undefined")
	endif()
	if (GPROF)
		target_compile_options(${LIBNAME} PRIVATE "-pg")
	endif()
	if (USE_LLD)
		target_link_libraries(${LIBNAME} "-fuse-ld=lld")
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
