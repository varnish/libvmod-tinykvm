cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)


add_vmod(vmod_kvm vmod_kvm.vcc "KVM Userspace Emulator VMOD"
	src/vmod_kvm.c
	src/kvm_backend.c
	src/kvm_live_update.c
	src/kvm_post.c
)

if (SHARED_KVM)
	target_link_libraries(vmod_kvm kvm_sandbox)
endif()

# VCL import name is "kvm"
add_vmod_tests(vmod_kvm kvm
	tests/alternate_programs.vtc
	tests/empty.vtc
	tests/failing_program.vtc
	tests/hello_backend_world.vtc
	tests/infinite_init.vtc
	tests/infinite_loop.vtc
	tests/infinite_storage.vtc
	tests/main_arguments.vtc
	tests/not_found.vtc
	tests/stateful_storage.vtc
	tests/stateful_buffered_storage.vtc
	tests/vmcommit.vtc
)
