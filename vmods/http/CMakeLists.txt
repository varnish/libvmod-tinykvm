cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_http)

add_vmod(vmod_http ${PATH}/vmod_http.vcc "HTTP VMOD"
	${PATH}/vmod_http.c
	${PATH}/http_alloc.c
	${PATH}/http_context.c
	${PATH}/http_handle.c
	${PATH}/http_header.c
	${PATH}/http_io.c
	${PATH}/http_object.c
	${PATH}/http_params.c
	${PATH}/http_pool.c
	${PATH}/http_url.c
	${PATH}/http_utils.c
)
target_link_libraries(vmod_http curl)

add_vmod_vsc(vmod_http
	${PATH}/VSC_vmod_http.vsc
)

# VCL import name is "http"
add_vmod_tests(vmod_http http
	# ...
)
