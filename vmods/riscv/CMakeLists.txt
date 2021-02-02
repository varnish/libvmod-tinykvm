cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)

set (RISCV_SOURCES
	src/vmod_riscv_response.c
	src/vmod_riscv_sandbox.c
	src/vmod_riscv_update.c
)

add_vmod(vmod_riscv vmod_riscv.vcc "RISC-V machine emulation VMOD"
	${RISCV_SOURCES}
)

if (SHARED_LIBRISCV)
	target_link_libraries(vmod_riscv sandbox)
endif()

# VCL import name is "riscv"
add_vmod_tests(vmod_riscv riscv
	tests/empty.vtc
	tests/hello_world.vtc
)
