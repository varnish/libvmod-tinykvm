cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_accounting)

add_vmod(vmod_accounting ${PATH}/vmod.vcc "Accounting VMOD"
	${PATH}/vmod_accounting.c
)

# VCL import name is "accounting"
add_vmod_tests(vmod_accounting accounting
)
