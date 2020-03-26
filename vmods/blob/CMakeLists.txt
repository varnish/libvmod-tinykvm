cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_blob)

add_vmod(vmod_blob ${PATH}/vmod_blob.vcc "Blob VMOD"
	${PATH}/base64.c
	${PATH}/hex.c
	${PATH}/id.c
	${PATH}/url.c
	${PATH}/vmod_blob.c
)
#add_vmod_vsc(vmod_blob ...)

# VCL import name is "blob"
add_vmod_tests(vmod_blob blob
	
)
