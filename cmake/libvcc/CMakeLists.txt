set(VCC_PATH ${VARNISH_PATH}/lib/libvcc)

# libvcc generated sources
set (LIBVCC_GEN_SOURCES
	${VCC_PATH}/vcc_fixed_token.c
	${VCC_PATH}/vcc_obj.c
	${VARNISH_PATH}/include/vcl.h
	${VARNISH_PATH}/include/vrt_obj.h
	${VARNISH_PATH}/include/tbl/vcl_returns.h
	${VARNISH_PATH}/include/tbl/vrt_stv_var.h
)
add_custom_command(
    COMMAND ${Python3_EXECUTABLE} ${VARNISH_PATH}/lib/libvcc/generate.py ${VARNISH_PATH} ${VARNISH_PATH}
    OUTPUT  ${LIBVCC_GEN_SOURCES}
    DEPENDS ${VARNISH_PATH}/lib/libvcc/generate.py
    COMMENT "Generating VCL compiler headers and sources"
)
add_custom_target(libvcc_generate
    DEPENDS ${LIBVCC_GEN_SOURCES}
)
set_source_files_properties(${LIBVCC_GEN_SOURCES}
                            PROPERTIES GENERATED TRUE)

## VCC library ##
set(VCC_SOURCES ${VCC_PATH}/vcc_acl.c
                ${VCC_PATH}/vcc_backend_util.c
                ${VCC_PATH}/vcc_fixed_token.c
                ${VCC_PATH}/vcc_storage.c
                ${VCC_PATH}/vcc_types.c
                ${VCC_PATH}/vcc_vmod.c
                ${VCC_PATH}/vcc_action.c
                ${VCC_PATH}/vcc_compile.c
                ${VCC_PATH}/vcc_obj.c
                ${VCC_PATH}/vcc_symb.c
                ${VCC_PATH}/vcc_utils.c
                ${VCC_PATH}/vcc_xref.c
                ${VCC_PATH}/vcc_backend.c
                ${VCC_PATH}/vcc_expr.c
                ${VCC_PATH}/vcc_parse.c
                ${VCC_PATH}/vcc_token.c
                ${VCC_PATH}/vcc_var.c
            )
add_library(vcc STATIC ${VCC_SOURCES})
target_include_directories(vcc PRIVATE ${VCC_PATH})
target_include_directories(vcc PRIVATE ${VARNISH_PATH}/include)
target_include_directories(vcc PRIVATE ${CONFIG_PATH})
target_link_libraries(vcc varnish) # we need the public API
add_dependencies(vcc libvcc_generate)
