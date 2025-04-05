add_vmod(vmod_kvm vmod_kvm.vcc "KVM Multi-Tenancy VMOD"
	kvm_backend.c
	kvm_live_update.c
	kvm_post.c
	to_string.c
	vmod_kvm.c
	../tinykvm/vmod_tinykvm_event.c
)
target_link_libraries(vmod_kvm kvm)
add_vmod_vsc(vmod_kvm VSC_vmod_kvm.vsc)

# KVM tests
enable_testing()
add_vmod_tests(vmod_kvm vmod_kvm
	tests/allowed_paths.vtc
	tests/alternate_programs.vtc
	#tests/async_task.vtc
	tests/backend_request.vtc
	tests/cached_post_backend.vtc
	#tests/curl_fetch.vtc
	tests/empty.vtc
	tests/error_handling.vtc
	tests/failing_program.vtc
	tests/hello_backend_world.vtc
	tests/http_fields.vtc
	#tests/http_illegal_fields.vtc
	tests/infinite_init.vtc
	tests/infinite_loop.vtc
	tests/infinite_storage.vtc
	tests/insane_settings.vtc
	#tests/live_update.vtc
	tests/main_arguments.vtc
	tests/max_work_memory.vtc
	tests/missing_storage.vtc
	tests/not_found.vtc
	tests/post_backend.vtc
	tests/program_not_ready.vtc
	tests/regex.vtc
	#tests/rotate_vcl.vtc
	tests/stateful_buffered_storage.vtc
	tests/stateful_storage.vtc
	tests/storage_failure.vtc
	tests/tenant_init.vtc
)
