cmake_minimum_required (VERSION 3.0.2)
project (vmod C CXX)
include(../vmod.cmake vmod)


add_vmod(vmod_kvm vmod_kvm.vcc "KVM Userspace Emulator VMOD"
	src/vmod_kvm.c
	src/vmod_kvm_adns.c
	src/kvm_backend.c
	src/kvm_live_update.c
	src/kvm_post.c
)

if (SHARED_KVM)
	target_link_libraries(vmod_kvm kvm_sandbox)
endif()

# VCL import name is "kvm"
add_vmod_tests(vmod_kvm kvm
	tests/allowed_paths.vtc
	tests/alternate_programs.vtc
	tests/async_task.vtc
	tests/empty.vtc
	tests/error_handling.vtc
	tests/failing_program.vtc
	tests/hello_backend_world.vtc
	#tests/hello_synth_world.vtc
	tests/http_fields.vtc
	tests/infinite_init.vtc
	tests/infinite_loop.vtc
	tests/infinite_storage.vtc
	tests/live_update.vtc
	tests/main_arguments.vtc
	tests/not_found.vtc
	tests/post_backend.vtc
	tests/program_not_ready.vtc
	tests/rotate_vcl.vtc
	tests/stateful_storage.vtc
	tests/stateful_buffered_storage.vtc
	tests/storage_failure.vtc
	#tests/max_work_memory.vtc
)
