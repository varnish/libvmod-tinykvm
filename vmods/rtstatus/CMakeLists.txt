cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_rtstatus)

add_custom_command(
	COMMAND ${PYTHON_EXECUTABLE} ${PATH}/cstring.py ${PATH}/rtstatus.html > vmod_rtstatus_html.h
	DEPENDS ${PATH}/cstring.py ${PATH}/rtstatus.html
	OUTPUT  vmod_rtstatus_html.h
)
add_custom_target(build_html_header DEPENDS vmod_rtstatus_html.h)

add_vmod(vmod_rtstatus ${PATH}/vmod_rtstatus.vcc "RT Status VMOD"
	${PATH}/vmod_rtstatus.c
)
add_dependencies(vmod_rtstatus build_html_header)

# VCL import name is "rtstatus"
add_vmod_tests(vmod_rtstatus rtstatus
)
