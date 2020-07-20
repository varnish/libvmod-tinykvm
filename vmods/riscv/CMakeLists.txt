cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)

add_vmod(vmod_riscv vmod_riscv.vcc "RISC-V machine emulation VMOD"
	src/vmod_riscv_exec.c
	src/vmod_riscv_sandbox.c
	src/linux.cpp
	src/sandbox.cpp
	src/script.cpp
	src/script_functions.cpp
	libriscv/emulator/src/linux.cpp
	libriscv/emulator/syscalls/src/syscalls.cpp
	libriscv/emulator/syscalls/src/threads.cpp
	libriscv/emulator/syscalls/src/posix_threads.cpp
	libriscv/emulator/syscalls/src/native_libc.cpp
	libriscv/emulator/syscalls/src/native_threads.cpp
)
target_compile_definitions(vmod_riscv PRIVATE
	NATIVE_SYSCALLS_BASE=80
	#THREADS_SYSCALL_BASE=90
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set_target_properties(vmod_riscv PROPERTIES CXX_STANDARD 17)
target_include_directories(vmod_riscv PRIVATE libriscv/emulator/syscalls)
target_include_directories(vmod_riscv PRIVATE libriscv/emulator/src)

add_subdirectory(libriscv/lib)
target_compile_definitions(riscv PUBLIC
	RISCV_THROW_UNIMPLEMENTED=1
	RISCV_EBREAK_MEANS_STOP=1
	RISCV_DISABLE_SYM_LOOKUP=1
	RISCV_SYSCALLS_MAX=100)
target_link_libraries(vmod_riscv riscv)

# WARNING: everything!
target_compile_options(riscv PUBLIC -march=native -Ofast -flto)

# VCL import name is "riscv"
add_vmod_tests(vmod_riscv riscv
	tests/startup.vtc
)
