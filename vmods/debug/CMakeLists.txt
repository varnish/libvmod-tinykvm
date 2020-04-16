cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_debug)

add_vmod(vmod_debug "${PATH}/vmod_debug.vcc" "Debug VMOD"
	${PATH}/vmod_debug.c
	${PATH}/vmod_debug_dyn.c
	${PATH}/vmod_debug_obj.c
)
add_vmod_vsc(vmod_debug ${PATH}/VSC_debug.vsc)

# VCL import name is "debug"
add_vmod_tests(vmod_debug debug
	
)
