cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_rewrite)

add_vmod(vmod_rewrite ${PATH}/vmod_rewrite.vcc "Rewrite VMOD"
	${PATH}/rewrite_methods.c
	${PATH}/vmod_rewrite.c
)

# VCL import name is "rewrite"
add_vmod_tests(vmod_rewrite rewrite
)
