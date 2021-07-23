cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)

add_vmod(vmod_kvm vmod_kvm.vcc "KVM Userspace Emulator VMOD"
	src/kvm_backend.c
	src/tenant.cpp
	src/fork.cpp
)

add_subdirectory(tinykvm)
target_link_libraries(vmod_kvm tinykvm)

#add_subdirectory(ext/json json)
target_link_libraries(vmod_kvm nlohmann_json)

# VCL import name is "kvm"
add_vmod_tests(vmod_kvm kvm
)
