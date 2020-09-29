cmake_minimum_required (VERSION 3.11)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_brotli)

add_vmod(vmod_brotli ${PATH}/vmod_brotli.vcc "Brotli VMOD"
	${PATH}/vmod_brotli.c
)

# VCL import name is "brotli"
add_vmod_tests(vmod_brotli brotli
)
