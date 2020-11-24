cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_debug)
if (VARNISH_PLUS)
	set (VMOD_VCC "${PATH}/vmod.vcc")
else()
	set (VMOD_VCC "${PATH}/vmod_debug.vcc")
endif()

add_vmod(vmod_debug ${VMOD_VCC} "Debug VMOD"
	${PATH}/vmod_debug.c
	${PATH}/vmod_debug_dyn.c
	${PATH}/vmod_debug_obj.c
)
if (VARNISH_PLUS)
	target_sources(vmod_debug PUBLIC
		${PATH}/vmod_debug_vfp.c
	)
endif()
add_vmod_vsc(vmod_debug ${PATH}/VSC_debug.vsc)

# VCL import name is "debug"
add_vmod_tests(vmod_debug debug
)
