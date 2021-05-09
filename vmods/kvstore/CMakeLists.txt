cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_kvstore)

add_vmod(vmod_kvstore ${PATH}/vmod_kvstore.vcc "KV-store VMOD"
	${PATH}/kvstore.c
	${PATH}/kvstore_file.c
	${PATH}/kvstore_scope.c
	${PATH}/kvstore_static.c
	${PATH}/kvstore_vcl.c
	${PATH}/kvstore_vsc.c
	${PATH}/vmod_kvstore.c
)

# VCL import name is "kvstore"
add_vmod_tests(vmod_kvstore kvstore
)
