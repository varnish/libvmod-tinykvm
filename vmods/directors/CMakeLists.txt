cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_directors)

add_vmod(vmod_directors "${PATH}/vmod_directors.vcc" "directors VMOD"
	${PATH}/fall_back.c
	${PATH}/hash.c
	${PATH}/random.c
	${PATH}/round_robin.c
	${PATH}/shard_cfg.c
	${PATH}/shard_dir.c
	${PATH}/vdir.c
	${PATH}/vmod_shard.c	
)
#add_vmod_vsc(vmod_directors ...)

# VCL import name is "directors"
add_vmod_tests(vmod_directors directors
	
)
