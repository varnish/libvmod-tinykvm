cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_goto)

add_vmod(vmod_goto ${PATH}/vmod_goto.vcc "Goto VMOD"
	${PATH}/vmod_goto.c
	${PATH}/goto_store.c
	${PATH}/dns_srv/dns_cache.c
	${PATH}/dns_srv/dns_local_search.c
	${PATH}/dns_srv/dns_nsswitch.c
	${PATH}/dns_srv/dns_query.c
#	${PATH}/dns_srv/dns_test.c
)
target_compile_definitions(vmod_goto PRIVATE _DNS_SRV_STANDALONE=1)
target_include_directories(vmod_goto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vmod_goto resolv)

# VCL import name is "goto"
add_vmod_tests(vmod_goto goto
)
