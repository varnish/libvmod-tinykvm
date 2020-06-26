cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_synthbackend)

add_vmod(vmod_synthbackend ${PATH}/vmod_synthbackend.vcc "Synthbackend VMOD"
	${PATH}/vmod_synthbackend.c
)

# VCL import name is "synthbackend"
add_vmod_tests(vmod_synthbackend synthbackend
)
