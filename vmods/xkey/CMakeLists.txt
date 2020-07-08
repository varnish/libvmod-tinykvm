cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/varnish-modules)

add_vmod(vmod_xkey ${PATH}/vmod_xkey.vcc "xkey VMOD"
	${PATH}/vmod_xkey.c
)

# VCL import name is "xkey"
add_vmod_tests(vmod_xkey xkey
)
