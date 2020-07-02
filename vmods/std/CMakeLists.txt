cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_std)
if (VARNISH_PLUS)
	set (VMOD_VCC "${PATH}/vmod.vcc")
else()
	set (VMOD_VCC "${PATH}/vmod_std.vcc")
endif()

add_vmod(vmod_std ${VMOD_VCC} "Standard VMOD"
	${PATH}/vmod_std.c
	${PATH}/vmod_std_conversions.c
	${PATH}/vmod_std_fileread.c
	${PATH}/vmod_std_querysort.c
)
# VCL import name is "std"
add_vmod_tests(vmod_std std
	# TODO: add these
)
