cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/varnish-modules)

add_vmod(vmod_bodyaccess ${PATH}/vmod_bodyaccess.vcc "Body Access VMOD"
	${PATH}/vmod_bodyaccess.c
)
# VCL import name is "bodyaccess"
add_vmod_tests(vmod_bodyaccess bodyaccess
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
	tests/test04.vtc
	tests/test05.vtc
)
