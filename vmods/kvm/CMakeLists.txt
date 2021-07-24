cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)

add_vmod(vmod_kvm vmod_kvm.vcc "KVM Userspace Emulator VMOD"
	src/vmod_kvm.c
	src/kvm_backend.c
	src/kvm_vcc_api.cpp
	src/backend.cpp
	src/machine_instance.cpp
	src/program_instance.cpp
	src/system_calls.cpp
	src/tenant.cpp
	src/tenant_instance.cpp
	tinykvm/src/functions.cpp
)

add_subdirectory(tinykvm)
target_compile_definitions(tinykvm PUBLIC ENABLE_GUEST_STDOUT=1)
set_property(TARGET tinykvm PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(vmod_kvm tinykvm)

#add_subdirectory(ext/json json)
target_link_libraries(vmod_kvm nlohmann_json)

# VCL import name is "kvm"
add_vmod_tests(vmod_kvm kvm
)
