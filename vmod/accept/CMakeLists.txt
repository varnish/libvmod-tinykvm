cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

add_vmod(vmod_accept vmod_accept.vcc "Accept VMOD"
	vmod_accept.c
)
# VCL import name is "accept"
vmod_add_tests(vmod_accept accept
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
)
