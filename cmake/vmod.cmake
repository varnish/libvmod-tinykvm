# settings
option(LIBFUZZER    "Build for libfuzzer" OFF)
option(VARNISH_PLUS "Build with Varnish plus" ON)
option(LOCAL_VC     "Build with local Varnish" ON)
set(VARNISH_DIR "/opt/varnish" CACHE STRING "Varnish installation")
# this needs to be set to build the std vmod:
set(VARNISH_SOURCE_DIR "" CACHE STRING "Varnish source directory")

# compiler flags
# NOTE: varnish uses non-standard features so use GNU
set(CMAKE_C_FLAGS "-Wall -Wextra -std=gnu11 -g -O2")

if (VARNISH_PLUS)
	if (LOCAL_VC)
		set(VMODTOOL "${VARNISH_SOURCE_DIR}/lib/libvcc/vmodtool.py")
		set(VINCLUDE "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VMODTOOL "${VARNISH_SOURCE_DIR}/share/varnish-plus/vmodtool.py")
		set(VINCLUDE "${VARNISH_DIR}/include/varnish-plus")
	endif()
else()
	if (LOCAL_VC)
		set(VMODTOOL "${VARNISH_SOURCE_DIR}/lib/libvcc/vmodtool.py")
		set(VINCLUDE "${VARNISH_SOURCE_DIR}/include")
	else()
		set(VMODTOOL "${VARNISH_DIR}/share/varnish/vmodtool.py")
		set(VINCLUDE "${VARNISH_DIR}/include/varnish")
	endif()
endif()

if (LOCAL_VC)
	set(VARNISHD ${VARNISH_SOURCE_DIR}/bin/varnishd/varnishd)
	set(VARNISHTEST ${VARNISH_SOURCE_DIR}/bin/varnishtest/varnishtest)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBVARNISH REQUIRED IMPORTED_TARGET varnishapi)
	find_program(VARNISHD    "varnishd")
	find_program(VARNISHTEST "varnishtest")
endif()

find_package(Threads)

# enable make test
enable_testing()

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
	target_include_directories(${LIBNAME} PUBLIC ${VINCLUDE})
	target_include_directories(${LIBNAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
	if (LOCAL_VC)
		target_include_directories(${LIBNAME} PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
		target_link_libraries(${LIBNAME} ${VARNISH_SOURCE_DIR}/lib/libvarnishapi/.libs/libvarnishapi.so)
	else()
		target_link_libraries(${LIBNAME} PkgConfig::LIBVARNISH)
	endif()
	target_link_libraries(${LIBNAME} Threads::Threads)
	target_compile_definitions(${LIBNAME} PRIVATE VMOD=1 HAVE_CONFIG_H)
	if (VARNISH_PLUS)
		target_compile_definitions(${LIBNAME} PRIVATE VARNISH_PLUS)
	endif()
	if (LIBFUZZER)
		target_compile_options(${LIBNAME} PRIVATE "-fsanitize=address,fuzzer")
		target_compile_definitions(${LIBNAME} PRIVATE LIBFUZZER_ENABLED=1)
	endif()
	# this will build the final .so files in the top build directory
	# you don't always want this, so it can probably be an option,
	# but in most cases this will be the right thing to do
	set_target_properties(${LIBNAME}
    	PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
endfunction()


function(vmod_add_tests LIBNAME IMPORT_NAME)
	set(LIBPATH "${CMAKE_BINARY_DIR}/lib${LIBNAME}.so")
	# varnishtest doesn't like to run with no tests
	if (ARGC GREATER 2) # not sure why 2 though
	add_test(NAME ${LIBNAME}_tests
		COMMAND ${VARNISHTEST} -j8 "-DVMOD_SO=\"${LIBPATH}\"" "-Dvarnishd=${VARNISHD}" ${ARGN}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
	endif()
endfunction()
