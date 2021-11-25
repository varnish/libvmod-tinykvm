cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_slicer)

add_vmod(vmod_slicer ${PATH}/vmod.vcc "Slicer VMOD"
	${PATH}/vmod_slicer.c
)

# VCL import name is "slicer"
add_vmod_tests(vmod_slicer slicer
)
