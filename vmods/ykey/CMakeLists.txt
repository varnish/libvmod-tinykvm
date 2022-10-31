cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_ykey)

add_vmod(vmod_ykey ${PATH}/vmod.vcc "ykey VMOD"
	${PATH}/vmod_ykey.c
)

# VCL import name is "ykey"
add_vmod_tests(vmod_ykey ykey
)
