cmake_minimum_required (VERSION 3.0.2)
project(varnishtest C)

set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -g -O2")
option(VARNISH_PLUS "Build with the varnish plus repo" ON)
