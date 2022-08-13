cmake_minimum_required (VERSION 3.8)
project (vmod C CXX)
include(../vmod.cmake vmod)

set (RISCV_SOURCES
	src/vmod_riscv.c
	src/vmod_riscv_backend.c
	src/vmod_riscv_update.c
)

add_vmod(vmod_riscv vmod_riscv.vcc "RISC-V sandbox VMOD"
	${RISCV_SOURCES}
)

if (SHARED_LIBRISCV)
	target_link_libraries(vmod_riscv sandbox)
endif()

# VCL import name is "riscv"
add_vmod_tests(vmod_riscv riscv
	tests/empty.vtc
	tests/hello_backend_world.vtc
	tests/hello_synth_world.vtc
	tests/faulty_loop.vtc
	#tests/goto_basic.vtc
)
