set (PATH ${VARNISH_PATH}/lib/libvarnishapi)

# libvarnishapi/vxp_fixed_token.c
add_custom_command(
    COMMAND ${Python3_EXECUTABLE} ${PATH}/generate.py
    WORKING_DIRECTORY ${PATH}
    OUTPUT  ${PATH}/vxp_fixed_token.c
    DEPENDS ${PATH}/generate.py
    COMMENT "Generating vxp_fixed_token.c"
)
add_custom_target(varnishapi_generate
    DEPENDS ${PATH}/vxp_fixed_token.c)

## libvarnishapi library ##
set (SOURCES
	${PATH}/daemon.c
	${PATH}/vsc.c
	${PATH}/vsc_format.c
	${PATH}/vsl_arg.c
	${PATH}/vsl.c
	${PATH}/vsl_cursor.c
	${PATH}/vsl_dispatch.c
	${PATH}/vsl_query.c
	${PATH}/vsm.c
	${PATH}/vst.c
	${PATH}/vut.c
	${PATH}/vxp.c
	${PATH}/vxp_fixed_token.c
	${PATH}/vxp_lexer.c
	${PATH}/vxp_parse.c
	#${PATH}/vsl2rst.c
	#${PATH}/vsl_glob_test.c
	#${PATH}/vxp_test.c
)
if (VARNISH_PLUS)
	list (APPEND SOURCES
		${PATH}/vst.c
    )
else()
	list (APPEND SOURCES
		${PATH}/vsig.c
    )
endif()

## libvarnishapi library ##
add_library(vapi_obj OBJECT ${SOURCES})
target_include_directories(vapi_obj PUBLIC ${VARNISH_PATH}/include)
target_include_directories(vapi_obj PUBLIC ${CONFIG_PATH})
target_compile_definitions(vapi_obj PUBLIC _XOPEN_SOURCE=700)
target_compile_definitions(vapi_obj PUBLIC -DNOT_IN_A_VMOD)
set_property(TARGET vapi_obj PROPERTY POSITION_INDEPENDENT_CODE 1)
add_dependencies(vapi_obj varnishapi_generate)

if (LIBFUZZER)
	target_compile_definitions(vapi_obj PUBLIC LIBFUZZER_ENABLED=1)
	target_compile_options(vapi_obj PRIVATE "${FUZZER_CFLAGS}")
elseif (SANITIZE)
	target_compile_options(vapi_obj PRIVATE "${SANITIZE_FLAGS}")
endif()

add_library(vapi_static STATIC $<TARGET_OBJECTS:vapi_obj>)
target_link_libraries(vapi_static varnish)

add_library(varnishapi  SHARED $<TARGET_OBJECTS:vapi_obj>)
target_include_directories(varnishapi PUBLIC ${CMAKE_BINARY_DIR}/libvarnish)
# build libvarnishapi.so into the top binary folder
set_target_properties(varnishapi
	PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	SOVERSION 1
)
if (LIBFUZZER)
	target_link_libraries(varnishapi PUBLIC "${FUZZER_LFLAGS}")
endif()
