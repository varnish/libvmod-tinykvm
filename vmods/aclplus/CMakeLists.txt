cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_aclplus)

add_vmod(vmod_aclplus ${PATH}/vmod_aclplus.vcc "ACLplus VMOD"
	${PATH}/aclplus_cidr.c
	${PATH}/vmod_aclplus.c
)

# VCL import name is "accept"
add_vmod_tests(vmod_accept accept
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
)
