cmake_minimum_required (VERSION 3.0.2)
project (sandbox CXX)

option(SHARED_LIBRISCV "Build the libriscv sandbox PIC" ON)

# WARNING: everything!
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++20 -O2 -g")

add_subdirectory(libriscv/lib)
target_compile_definitions(riscv PUBLIC
	RISCV_THROW_UNIMPLEMENTED=1
	RISCV_EBREAK_MEANS_STOP=1
	RISCV_DISABLE_SYM_LOOKUP=1
	RISCV_SYSCALLS_MAX=100)

if (NATIVE)
	target_compile_options(riscv PUBLIC -march=native -Ofast)
	target_compile_options(EASTL PUBLIC -march=native -Ofast)
endif()
if (LTO_ENABLE)
	target_compile_options(riscv PUBLIC -flto)
	target_compile_options(EASTL PUBLIC -flto)
endif()

list(APPEND RISCV_SOURCES
	src/linux.cpp
	src/sandbox.cpp
	src/script.cpp
	src/script_functions.cpp
	libriscv/emulator/syscalls/src/syscalls.cpp
	libriscv/emulator/syscalls/src/threads.cpp
	libriscv/emulator/syscalls/src/posix_threads.cpp
	libriscv/emulator/syscalls/src/native_libc.cpp
	libriscv/emulator/syscalls/src/native_threads.cpp
)

add_library(sandbox STATIC ${RISCV_SOURCES})
set_target_properties(sandbox PROPERTIES CXX_STANDARD 17)
target_include_directories(sandbox PRIVATE ${VARNISH_PATH}/include)
target_include_directories(sandbox PRIVATE libriscv/emulator/syscalls)
target_compile_definitions(sandbox PRIVATE
	NATIVE_SYSCALLS_BASE=80
)
target_link_libraries(sandbox PUBLIC riscv stdc++)

if (SHARED_LIBRISCV)
	set_property(TARGET EASTL PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET riscv PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET sandbox PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
