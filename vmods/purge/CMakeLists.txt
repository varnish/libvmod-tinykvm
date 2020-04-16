cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_purge)

add_vmod(vmod_purge "${PATH}/vmod_purge.vcc" "Purge VMOD"
	${PATH}/vmod_purge.c
)
#add_vmod_vsc(vmod_purge ...)

# VCL import name is "purge"
add_vmod_tests(vmod_purge purge
	
)
