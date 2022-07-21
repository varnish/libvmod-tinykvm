set (LIBPATH ${VARNISH_PATH}/lib)
set (VARNISHD_PATH ${VARNISH_PATH}/bin/varnishd)

set(OVERRIDE_VMOD_API "" CACHE STRING "Overrides vmod_api string")
set(VSTATE_DIR "/tmp/varnishd" CACHE STRING "Default state directory")
set(VMOD_DIR   "/opt/varnish/lib/varnish-plus/vmods" CACHE STRING "Default VMOD directory")
set(VCL_DIR    "/opt/varnish/share/varnish-plus/vcl" CACHE STRING "Default VCL directory")

# include/vcs_version.h include/vmod_abi.h
execute_process(COMMAND git rev-parse HEAD
				WORKING_DIRECTORY ${VARNISH_PATH}
				OUTPUT_VARIABLE GIT_REPO_VERSION
				OUTPUT_STRIP_TRAILING_WHITESPACE)
if (OVERRIDE_VMOD_API)
	set(GIT_REPO_VERSION "${OVERRIDE_VMOD_API}")
endif()
configure_file("vcs_version.h.in" "${CMAKE_BINARY_DIR}/vcs_version.h")
configure_file("vmod_abi.h.in" "${CMAKE_BINARY_DIR}/vmod_abi.h")

## libvarnish library ##
set (SOURCES
	${LIBPATH}/libvarnish/vas.c
	${LIBPATH}/libvarnish/vav.c
	${LIBPATH}/libvarnish/vcli_proto.c
	${LIBPATH}/libvarnish/vcli_serve.c
	${LIBPATH}/libvarnish/vct.c
	${LIBPATH}/libvarnish/version.c
	${LIBPATH}/libvarnish/vev.c
	${LIBPATH}/libvarnish/vfil.c
	${LIBPATH}/libvarnish/vfl.c
	${LIBPATH}/libvarnish/vin.c
	${LIBPATH}/libvarnish/vjsn.c
	${LIBPATH}/libvarnish/vlu.c
	${LIBPATH}/libvarnish/vmb.c
	${LIBPATH}/libvarnish/vnum.c
	${LIBPATH}/libvarnish/vpf.c
	${LIBPATH}/libvarnish/vre.c
	${LIBPATH}/libvarnish/vrnd.c
	${LIBPATH}/libvarnish/vsa.c
	${LIBPATH}/libvarnish/vsb.c
	${LIBPATH}/libvarnish/vsha256.c
	${LIBPATH}/libvarnish/vss.c
	${LIBPATH}/libvarnish/vsub.c
	${LIBPATH}/libvarnish/vtcp.c
	${LIBPATH}/libvarnish/vtim.c
	${LIBPATH}/libvarnish/vus.c
)
if (NOT VARNISH_PLUS)
	list (APPEND SOURCES
#        ${LIBPATH}/libvarnish/verrno.c
		${LIBPATH}/libvarnish/vbh.c
		${LIBPATH}/libvarnish/venc.c
		${LIBPATH}/libvarnish/vte.c
	)
else()
	list (APPEND SOURCES
		${LIBPATH}/libvarnish/binary_heap.c
		${LIBPATH}/libvarnish/vjsn.c
	)
endif()

## VSC generation (used by libvarnish) ##
set(VSC_SOURCES
	VSC_lck.vsc
	VSC_main.vsc
	VSC_mempool.vsc
	VSC_mgt.vsc
	VSC_sma.vsc
	VSC_smf.vsc
	VSC_smu.vsc
	VSC_vbe.vsc
)
if (VARNISH_PLUS)
	list(APPEND VSC_SOURCES
		VSC_accg.vsc
		VSC_accg_diag.vsc
		VSC_brotli.vsc
		VSC_mse.vsc
		VSC_mse_book.vsc
		VSC_mse_store.vsc
	)
endif()
function(build_vsc_file NAME)
	get_filename_component(VSC_FILE ${NAME} NAME_WE)
	add_custom_command(
			OUTPUT ${VSC_FILE}.c ${VSC_FILE}.h
			COMMAND python3 "${LIBPATH}/libvsc/vsctool.py" -ch ${NAME}
			DEPENDS ${NAME}
		)
endfunction()
foreach(VSC_FILE ${VSC_SOURCES})
	build_vsc_file("${LIBPATH}/libvsc/${VSC_FILE}")
	get_filename_component(VSC_WEFILE ${VSC_FILE} NAME_WE)
	list(APPEND VSC_COMPILED
		${CMAKE_CURRENT_BINARY_DIR}/${VSC_WEFILE}.c
		${CMAKE_CURRENT_BINARY_DIR}/${VSC_WEFILE}.h
	)
endforeach()
set_source_files_properties(${VSC_COMPILED} PROPERTIES GENERATED TRUE)

## libvarnish library ##
add_library(varnish STATIC ${SOURCES} ${VSC_COMPILED})
target_include_directories(varnish PUBLIC ${VARNISHD_PATH})
target_include_directories(varnish PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnish PUBLIC ${CONFIG_PATH})
target_include_directories(varnish PUBLIC ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(varnish PUBLIC _XOPEN_SOURCE=700)
target_compile_definitions(varnish PUBLIC VARNISH_STATE_DIR="${VSTATE_DIR}")
target_compile_definitions(varnish PUBLIC VARNISH_VMOD_DIR="${VMOD_DIR}")
target_compile_definitions(varnish PUBLIC VARNISH_VCL_DIR="${VCL_DIR}")
target_compile_definitions(varnish PUBLIC NOT_IN_A_VMOD=1)
target_compile_definitions(varnish PRIVATE HAVE_REUSEPORT=1)
if (VARNISH_PLUS)
	target_link_libraries(varnish brotlidec brotlienc pcre m)
else()
	target_link_libraries(varnish pcre2-8 m)
	# Brilliant idea to require this random global define
	target_compile_definitions(varnish PUBLIC _lint=1)
endif()
#set_property(TARGET varnish PROPERTY POSITION_INDEPENDENT_CODE 1)

if (LIBFUZZER)
	if (NOT "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
		message(FATAL_ERROR "You can only use Clang when libfuzzer is enabled")
	endif()
	target_compile_definitions(varnish PUBLIC LIBFUZZER_ENABLED=1)
	target_compile_options(varnish PRIVATE ${FUZZER_CFLAGS})
elseif (SANITIZE)
	target_compile_definitions(varnish PUBLIC SANITIZERS_ENABLED=1)
	target_compile_options(varnish PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(varnish "${SANITIZE_FLAGS}")
endif()
