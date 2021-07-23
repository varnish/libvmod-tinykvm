cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_utils)

add_vmod(vmod_utils "${PATH}/vmod_utils.vcc" "Utils VMOD"
	${PATH}/vmod_utils.c
	${PATH}/vmod_base64.c
	${PATH}/base64.c
	${PATH}/probe.c
)

# VCL import name is "utils"
add_vmod_tests(vmod_utils utils
)
