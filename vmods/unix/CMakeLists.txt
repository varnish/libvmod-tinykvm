cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_unix)
# confusion solution
set(VMOD_UNIX_VCC "${PATH}/vmod_unix.vcc")
if (VARNISH_PLUS)
	set(VMOD_UNIX_VCC "${PATH}/vmod.vcc")
endif()

add_vmod(vmod_unix "${VMOD_UNIX_VCC}" "Unix VMOD"
	${PATH}/vmod_unix.c
)

# VCL import name is "unix"
add_vmod_tests(vmod_unix unix
)
