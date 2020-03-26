cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_urlplus)

add_vmod(vmod_urlplus ${PATH}/vmod_urlplus.vcc "URLplus VMOD"
	${PATH}/urlplus_as_string.c
	${PATH}/urlplus_parse.c
	${PATH}/urlplus_utils.c
	${PATH}/vmod_urlplus.c
	${PATH}/vmod_urlplus_query.c
	${PATH}/vmod_urlplus_url.c
)
# VCL import name is "urlplus"
add_vmod_tests(vmod_urlplus urlplus
)
