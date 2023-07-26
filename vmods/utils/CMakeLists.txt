cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_utils)
set (TESTS ${VARNISH_SOURCE_DIR}/bin/varnishtest/vmodtests/utils)

add_vmod(vmod_utils "${PATH}/vmod_utils.vcc" "Utils VMOD"
	${PATH}/vmod_utils.c
	${PATH}/vmod_base64.c
	${PATH}/base64.c
	${PATH}/probe.c
)

# VCL import name is "utils"
add_vmod_tests(vmod_utils utils
	${TESTS}/test01.vtc
	${TESTS}/test02.vtc
	${TESTS}/test03.vtc
	${TESTS}/test04.vtc
	${TESTS}/test05.vtc
	${TESTS}/test06.vtc
	${TESTS}/test07.vtc
	${TESTS}/test08.vtc
	${TESTS}/test09.vtc
	${TESTS}/test10.vtc
	${TESTS}/test11.vtc
	${TESTS}/test12.vtc
	${TESTS}/test13.vtc
	${TESTS}/test14.vtc
	${TESTS}/test15.vtc
	${TESTS}/test16.vtc
	${TESTS}/test17.vtc
	${TESTS}/test18.vtc
	${TESTS}/test19.vtc
	${TESTS}/test20.vtc
)
