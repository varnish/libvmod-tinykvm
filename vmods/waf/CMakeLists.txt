cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

add_vmod(vmod_waf vmod_waf.vcc "WAF/ModSecurity VMOD"
	vmod_waf.c
)

add_subdirectory(modsec)
target_link_libraries(vmod_waf modsecurity)

add_vmod_tests(vmod_waf waf
	tests/test01.vtc  tests/test05.vtc  tests/test09.vtc  tests/test13.vtc  tests/test17.vtc
	tests/test02.vtc  tests/test06.vtc  tests/test10.vtc  tests/test14.vtc  tests/test18.vtc
	tests/test03.vtc  tests/test07.vtc  tests/test11.vtc  tests/test15.vtc  tests/test19.vtc
	tests/test04.vtc  tests/test08.vtc  tests/test12.vtc  tests/test16.vtc
)
