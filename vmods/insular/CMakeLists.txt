cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)


add_vmod(vmod_insular vmod_insular.vcc "Insular Cache Configuration VMOD"
	src/vmod_insular.c
)

if (SHARED_KVM)
	target_link_libraries(vmod_insular kvm_sandbox)
endif()

# VCL import name is "insular"
add_vmod_tests(vmod_insular insular
)
