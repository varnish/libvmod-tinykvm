cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_vtc)

add_vmod(vmod_vtc "${PATH}/vmod.vcc" "Varnish Test-Case VMOD"
	${PATH}/vmod_vtc.c
)

# VCL import name is "vtc"
add_vmod_tests(vmod_vtc vtc
)
