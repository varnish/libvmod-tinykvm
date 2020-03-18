cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

set (PATH ../../ext/varnish-cache-plus/lib/libvmod_jwt)

add_vmod(vmod_jwt ${PATH}/vmod_jwt.vcc "JWT VMOD"
	${PATH}/jwt_base64.c
	${PATH}/jwt_blob.c
	${PATH}/jwt_hash.c
	${PATH}/jwt_hmac.c
	${PATH}/jwt_rsa.c
	${PATH}/jwt_str.c
	${PATH}/vmod_jwt.c
	${PATH}/vmod_jwt_reader.c
	${PATH}/vmod_jwt_writer.c
)

target_link_libraries(vmod_jwt vjson)

# VCL import name is "jwt"
vmod_add_tests(vmod_jwt jwt
)
