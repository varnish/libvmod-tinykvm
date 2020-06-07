cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_proxy)

add_vmod(vmod_proxy "${PATH}/vmod.vcc" "Cookie VMOD"
	${PATH}/vmod_proxy.c
)

# VCL import name is "proxy"
add_vmod_tests(vmod_proxy proxy
)
