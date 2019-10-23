cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

add_vmod(vmod_bodyaccess vmod_bodyaccess.vcc "Body Access VMOD"
	vmod_bodyaccess.c
)
# VCL import name is "bodyaccess"
vmod_add_tests(vmod_bodyaccess bodyaccess
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
	tests/test04.vtc
	tests/test05.vtc
)
