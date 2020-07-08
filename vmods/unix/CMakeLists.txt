cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_unix)

add_vmod(vmod_unix "${PATH}/vmod.vcc" "Unix VMOD"
	${PATH}/vmod_unix.c
)

# VCL import name is "unix"
add_vmod_tests(vmod_unix unix
)
