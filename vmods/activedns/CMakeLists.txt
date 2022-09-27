cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_activedns)

add_vmod(vmod_activedns ${PATH}/vmod_activedns.vcc "ActiveDNS VMOD"
	${PATH}/vmod_activedns.c
)

target_link_libraries(vmod_activedns adns)

# VCL import name is "activedns"
add_vmod_tests(vmod_activedns activedns

)
