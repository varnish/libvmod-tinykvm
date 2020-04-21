cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_headerplus)

add_vmod(vmod_headerplus "${PATH}/vmod_headerplus.vcc" "HeaderPlus VMOD"
	${PATH}/hp_utils.c
	${PATH}/vmod_headerplus.c
	${PATH}/vmod_hp_attr.c
	${PATH}/vmod_hp_create.c
	${PATH}/vmod_hp_delete.c
	${PATH}/vmod_hp_json.c
	${PATH}/vmod_hp_read.c
	${PATH}/vmod_hp_update.c
)
target_link_libraries(vmod_headerplus vjson)

# VCL import name is "headerplus"
add_vmod_tests(vmod_headerplus headerplus
	tests/test01.vtc
	tests/test02.vtc
	tests/test03.vtc
	tests/test04.vtc
	tests/test05.vtc
	tests/test06.vtc
	tests/test07.vtc
	tests/test08.vtc
	tests/test09.vtc
	tests/test10.vtc
	tests/test11.vtc
)
