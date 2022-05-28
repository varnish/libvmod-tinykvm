cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_http)

add_vmod(vmod_http ${PATH}/vmod_http.vcc "HTTP VMOD"
	${PATH}/vmod_http.c
	${PATH}/http_engine.c
	${PATH}/http_params.c
	${PATH}/http_req.c
	${PATH}/http_url.c
)
target_link_libraries(vmod_http curl)

add_vmod_vsc(vmod_http
	${PATH}/VSC_vmod_http.vsc
)

# VCL import name is "http"
add_vmod_tests(vmod_http http
	# ...
)
