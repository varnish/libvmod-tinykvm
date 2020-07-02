cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_vtc)
if (VARNISH_PLUS)
	set (VMOD_VCC "${PATH}/vmod.vcc")
else()
	set (VMOD_VCC "${PATH}/vmod_vtc.vcc")
endif()

add_vmod(vmod_vtc ${VMOD_VCC} "Varnish Test-Case VMOD"
	${PATH}/vmod_vtc.c
)

# VCL import name is "vtc"
add_vmod_tests(vmod_vtc vtc
)
