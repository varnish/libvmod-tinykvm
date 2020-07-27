cmake_minimum_required (VERSION 3.0.2)
project (sandbox CXX)

option(SHARED_LIBRISCV "Build the libriscv sandbox as a VMOD" OFF)

add_subdirectory(libriscv/lib)
target_compile_definitions(riscv PUBLIC
	RISCV_THROW_UNIMPLEMENTED=1
	RISCV_EBREAK_MEANS_STOP=1
	RISCV_DISABLE_SYM_LOOKUP=1
	RISCV_SYSCALLS_MAX=100)

# WARNING: everything!
target_compile_options(riscv PUBLIC -march=native -Ofast -flto)

list(APPEND RISCV_SOURCES
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

add_library(sandbox STATIC ${RISCV_SOURCES})
set_target_properties(sandbox PROPERTIES CXX_STANDARD 20)
target_include_directories(sandbox PRIVATE ${VARNISH_PATH}/include)
target_include_directories(sandbox PRIVATE libriscv/emulator/syscalls)
target_include_directories(sandbox PRIVATE libriscv/emulator/src)
target_compile_definitions(sandbox PRIVATE
	NATIVE_SYSCALLS_BASE=80
)
target_link_libraries(sandbox PUBLIC riscv)

if (SHARED_LIBRISCV)
	set_property(TARGET EASTL PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET riscv PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET sandbox PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
