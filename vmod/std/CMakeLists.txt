cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

set (PATH ../../ext/varnish-cache-plus/lib/libvmod_std)

add_vmod(vmod_std vmod_std.vcc "Standard VMOD"
	${PATH}/vmod_std.c
	${PATH}/vmod_std_conversions.c
	${PATH}/vmod_std_fileread.c
	${PATH}/vmod_std_querysort.c
)
# VCL import name is "std"
vmod_add_tests(vmod_std std
)
