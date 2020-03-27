cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set(PATH ${VARNISH_PATH}/lib/libvmod_cookie)

add_vmod(vmod_cookie ${PATH}/vmod_cookie.vcc "Cookie VMOD"
	${PATH}/vmod_cookie.c
)

# VCL import name is "cookie"
add_vmod_tests(vmod_cookie cookie
	${PATH}/tests/cookie_b00000.vtc
	${PATH}/tests/cookie_b00001.vtc
	${PATH}/tests/cookie_b00002.vtc
	${PATH}/tests/cookie_b00003.vtc
	${PATH}/tests/cookie_b00004.vtc
	${PATH}/tests/cookie_b00005.vtc
	${PATH}/tests/cookie_b00006.vtc
	${PATH}/tests/cookie_b00007.vtc
	${PATH}/tests/cookie_b00008.vtc
	${PATH}/tests/cookie_b00009.vtc
	${PATH}/tests/cookie_b00010.vtc
	${PATH}/tests/cookie_b00011.vtc
	${PATH}/tests/cookie_b00012.vtc
	${PATH}/tests/cookie_b00013.vtc
	${PATH}/tests/cookie_r00028.vtc
	${PATH}/tests/cookie_v00000.vtc
)
