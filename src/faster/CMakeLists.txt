cmake_minimum_required (VERSION 3.18.0)
project (faster CXX)

#option(SHARED_LIBRISCV "Build the libriscv sandbox as a VMOD" OFF)

# WARNING: everything!
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++20 -O2 -g")

add_subdirectory(fmt)

if (NATIVE)
	target_compile_options(fmt PUBLIC -march=native -Ofast)
endif()
if (LTO_ENABLE)
	target_compile_options(fmt PUBLIC -flto)
endif()

list(APPEND FASTER_SOURCES
	faster_http.cpp
)

add_library(faster STATIC ${FASTER_SOURCES})
set_target_properties(faster PROPERTIES CXX_STANDARD 20)
target_include_directories(faster PRIVATE ${VARNISH_PATH}/include)
target_include_directories(faster PRIVATE ${VARNISH_PATH}/bin/varnishd)
target_compile_definitions(faster PRIVATE
	NATIVE_SYSCALLS_BASE=80
)
target_link_libraries(faster PUBLIC fmt)
