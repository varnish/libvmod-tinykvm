cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_accept)
message(STATUS "VMOD accept path: ${PATH}")
add_vmod(vmod_accept ${PATH}/vmod_accept.vcc "Accept VMOD"
	${PATH}/vmod_accept.c
)
# VCL import name is "accept"
add_vmod_tests(vmod_accept accept
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
)
