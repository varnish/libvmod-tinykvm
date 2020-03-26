cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

add_vmod(vmod_header vmod_header.vcc "Header VMOD"
	vmod_header.c
)
# VCL import name is "header"
add_vmod_tests(vmod_header header
	tests/append.vtc
	tests/copy.vtc
	tests/get.vtc
	tests/import.vtc
	tests/keep-others.vtc
	tests/regsub.vtc
	tests/remove.vtc
	tests/some-data.vtc
)
