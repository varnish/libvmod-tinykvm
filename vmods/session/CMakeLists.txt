cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_session)

add_vmod(vmod_session ${PATH}/vmod_session.vcc "Session VMOD"
	${PATH}/vmod_session.c
)

# VCL import name is "session"
add_vmod_tests(vmod_session session
)
