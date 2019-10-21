cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../cmake/varnish.cmake vmod)

add_vmod(vmod_example vmod_example.vcc "Example VMOD"
	vmod_example.c
)
# VCL import name is "example"
vmod_add_tests(vmod_example example
	tests/test01.vtc
)
