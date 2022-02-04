cmake_minimum_required (VERSION 3.8)
project (sandbox CXX)

option(SHARED_LIBRISCV "Build the RISC-V sandbox as a shared library" ON)

# WARNING: everything!
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++20 -O2 -g")

list(APPEND RISCV_SOURCES
	src/machine_instance.cpp
	src/sandbox.cpp
	src/script.cpp
	src/script_debug.cpp
	src/script_functions.cpp
	src/script_sha256.cpp
	src/tenancy.cpp
	src/vmod_riscv.cpp
	src/varnish_interface.c
	src/vmods/goto.cpp
)

add_library(sandbox STATIC ${RISCV_SOURCES})
set_target_properties(sandbox PROPERTIES CXX_STANDARD 17)
target_include_directories(sandbox PRIVATE ${VARNISH_PATH}/include)
target_include_directories(sandbox PRIVATE ${VARNISH_PATH}/bin/varnishd)
target_compile_definitions(sandbox PRIVATE
	NATIVE_SYSCALLS_BASE=80
)


option(RISCV_EXPERIMENTAL "" ON)
option(RISCV_SHARED_PT "" ON)

add_subdirectory(libriscv/lib)
target_compile_definitions(riscv PUBLIC
	RISCV_EBREAK_MEANS_STOP=1
	RISCV_DISABLE_SYM_LOOKUP=1
	RISCV_SYSCALLS_MAX=100)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
add_subdirectory(ext/json)


if (NATIVE)
	target_compile_options(riscv PUBLIC -march=native -Ofast -fno-fast-math)
endif()
if (LTO_ENABLE)
	target_compile_options(riscv PUBLIC -flto)
endif()
if (DEBUGGING)
	target_compile_options(riscv PUBLIC -O1 -ggdb3)
	target_compile_options(riscv PUBLIC ${SANITIZE_FLAGS})
endif()

target_link_libraries(sandbox PRIVATE nlohmann_json riscv stdc++)

if (SHARED_LIBRISCV)
	set_property(TARGET riscv PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET sandbox PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
