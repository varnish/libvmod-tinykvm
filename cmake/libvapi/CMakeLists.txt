set (PATH ${VARNISH_PATH}/lib/libvarnishapi)

# libvarnishapi/vxp_fixed_token.c
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${PATH}/generate.py
    WORKING_DIRECTORY ${PATH}
    OUTPUT  ${PATH}/vxp_fixed_token.c
    DEPENDS ${PATH}/generate.py
    COMMENT "Generating vxp_fixed_token.c"
)
add_custom_target(varnishapi_generate
    DEPENDS ${PATH}/vxp_fixed_token.c
            includes_generate
    )

## libvarnishapi library ##
set (SOURCES
	${PATH}/daemon.c
	${PATH}/vsc.c
	${PATH}/vsl2rst.c
	${PATH}/vsl_arg.c
	${PATH}/vsl.c
	${PATH}/vsl_cursor.c
	${PATH}/vsl_dispatch.c
	${PATH}/vsl_glob_test.c
	${PATH}/vsl_query.c
	${PATH}/vsm.c
	${PATH}/vut.c
	${PATH}/vxp.c
	${PATH}/vxp_fixed_token.c
	${PATH}/vxp_lexer.c
	${PATH}/vxp_parse.c
	${PATH}/vxp_test.c
)
if (VARNISH_PLUS)
	set (SOURCES ${SOURCES}
		${PATH}/vst.c
    )
else()
    set (SOURCES ${SOURCES}
		${PATH}/vsig.c
    )
endif()

## libvarnishapi library ##
add_library(varnishapi ${SOURCES})
target_include_directories(varnishapi PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnishapi PUBLIC ${CONFIG_PATH})
target_compile_definitions(varnishapi PUBLIC _XOPEN_SOURCE=700)
target_compile_definitions(varnishapi PUBLIC -DNOT_IN_A_VMOD)
add_dependencies(varnishapi varnishapi_generate)
target_link_libraries(varnishapi varnish)

if (SANITIZE)
	target_compile_options(varnishapi PRIVATE "${SANITIZE_FLAGS}")
endif()
