cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_str)

add_vmod(vmod_str ${PATH}/vmod_str.vcc "String VMOD"
	${PATH}/vmod_str.c
)

# VCL import name is "str"
add_vmod_tests(vmod_str str
)
