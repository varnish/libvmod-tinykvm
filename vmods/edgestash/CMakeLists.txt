cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_edgestash)
set (ESPATH ${VARNISH_SOURCE_DIR}/lib/libedgestash)

set(LIBES_SOURCES
	${ESPATH}/ves_compute.c
	${ESPATH}/ves_json.c
	${ESPATH}/ves_json_nav.c
	${ESPATH}/ves_json_util.c
	${ESPATH}/ves_parse.c
	${ESPATH}/ves_var.c
	${ESPATH}/ves_vec_gen.c
)
add_library(edgestash ${LIBES_SOURCES})
target_include_directories(edgestash PRIVATE ${CMAKE_BINARY_DIR})
target_include_directories(edgestash PRIVATE ${CMAKE_SOURCE_DIR}/cmake/include)
target_include_directories(edgestash PRIVATE ${VARNISH_SOURCE_DIR}/include)
target_include_directories(edgestash PRIVATE ${VARNISH_SOURCE_DIR}/include/edgestash)
target_include_directories(edgestash PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
target_link_libraries(edgestash vjson)

add_vmod(vmod_edgestash ${PATH}/vmod_edgestash.vcc "Edgestash VMOD"
	${PATH}/cache_subreq.c
	${PATH}/vmod_edgestash.c
	${PATH}/vdp_edgestash.c
	${PATH}/vfp_edgestash.c
)
target_include_directories(vmod_edgestash PRIVATE varnish)
target_link_libraries(vmod_edgestash edgestash)

# VCL import name is "edgestash"
add_vmod_tests(vmod_edgestash edgestash
)
