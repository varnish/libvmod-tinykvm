cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_file)

add_vmod(vmod_file ${PATH}/vmod_file.vcc "File VMOD"
	${PATH}/file_acl.c
	${PATH}/file_dir.c
	${PATH}/file_mimedb.c
	${PATH}/file_utils.c
	${PATH}/file_vfp.c
	${PATH}/vmod_file.c
)

# VCL import name is "file"
add_vmod_tests(vmod_file file
)
