cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../../cmake/vmod.cmake vmod)

add_vmod(vmod_cookie vmod_cookie.vcc "Cookie VMOD"
	vmod_cookie.c
)
# VCL import name is "cookie"
add_vmod_tests(vmod_cookie cookie
	tests/01-set-delete-produce.vtc
	tests/02-clean.vtc
	tests/03-null.vtc
	tests/04-filter_except.vtc
	tests/05-get.vtc
	tests/05-filter.vtc
	tests/06-concurrent-query.vtc
	tests/07-isset.vtc
	tests/08-overflow.vtc
	tests/10-parser.vtc
	tests/30-format_1123.vtc
	tests/r28.vtc
)
