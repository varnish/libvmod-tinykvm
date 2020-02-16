cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

add_vmod(vmod_waf vmod_waf.vcc "WAF/ModSecurity VMOD"
	vmod_waf.c
)
# VCL import name is "waf"
vmod_add_tests(vmod_waf waf
)

add_subdirectory(modsec)
target_link_libraries(vmod_waf modsecurity)
