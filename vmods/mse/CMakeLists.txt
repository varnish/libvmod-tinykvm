cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_mse)

add_vmod(vmod_mse "${PATH}/vmod_mse.vcc" "MSE VMOD"
	${PATH}/vmod_mse.c
)

# VCL import name is "mse"
add_vmod_tests(vmod_mse mse
)
