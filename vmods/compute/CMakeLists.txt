cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)


add_vmod(vmod_compute vmod_compute.vcc "High-Performance Compute VMOD"
	src/vmod_compute.c
	src/vmod_compute_event.c
	../kvm/src/kvm_backend.c
	../kvm/src/kvm_post.c
	../kvm/src/to_string.c
)

if (SHARED_KVM)
	target_link_libraries(vmod_compute kvm_sandbox)
endif()

# VCL import name is "compute"
add_vmod_tests(vmod_compute compute
	tests/remote_archive.vtc
)
