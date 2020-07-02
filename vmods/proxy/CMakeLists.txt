cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_proxy)
if (VARNISH_PLUS)
	set (VMOD_VCC "${PATH}/vmod.vcc")
else()
	set (VMOD_VCC "${PATH}/vmod_proxy.vcc")
endif()

add_vmod(vmod_proxy ${VMOD_VCC} "Cookie VMOD"
	${PATH}/vmod_proxy.c
)

# VCL import name is "proxy"
add_vmod_tests(vmod_proxy proxy
)
