cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

add_vmod(vmod_std vmod_std.vcc "Standard VMOD"
	vmod_std.c
	vmod_std_conversions.c
	vmod_std_fileread.c
	vmod_std_querysort.c
)
# VCL import name is "std"
vmod_add_tests(vmod_std std
)
