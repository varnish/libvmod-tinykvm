cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_resolver)

add_vmod(vmod_resolver ${PATH}/vmod_resolver.vcc "Resolver VMOD"
	${PATH}/resolver_utils.c
	${PATH}/vmod_resolver.c
)

# VCL import name is "accept"
add_vmod_tests(vmod_resolver resolver
#	resolver.vtc
)
