add_vmod(vmod_tinykvm vmod_tinykvm.vcc "High-Performance Compute VMOD"
	../kvm/kvm_backend.c
	../kvm/kvm_live_update.c
	../kvm/kvm_post.c
	../kvm/to_string.c
	vmod_tinykvm.c
	vmod_tinykvm_event.c
)
target_link_libraries(vmod_tinykvm kvm)
add_vmod_vsc(vmod_tinykvm VSC_vmod_kvm.vsc)

# Compute tests
enable_testing()
add_vmod_tests(vmod_tinykvm vmod_tinykvm
	tests/minimal_example.vtc
	tests/remote_archive.vtc
	tests/synth.vtc
	tests/warmup.vtc
)
