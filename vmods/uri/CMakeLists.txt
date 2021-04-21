cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_uri)

add_vmod(vmod_uri ${PATH}/vmod_uri.vcc "Accept VMOD"
	${PATH}/uri_as_string.c
	${PATH}/uri_decode.c
	${PATH}/uri_normalize.c
	${PATH}/uri_parse.c
	${PATH}/uri_utils.c
	${PATH}/vmod_uri.c
)

# VCL import name is "uri"
add_vmod_tests(vmod_uri uri
)
