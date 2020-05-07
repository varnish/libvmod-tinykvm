cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

add_vmod(vmod_wssan wssan.vcc "Workspace Sanitizer VMOD"
	wssan.c
)

# VCL import name is "wssan"
add_vmod_tests(vmod_wssan wssan
	tests/hmm.vtc
)
