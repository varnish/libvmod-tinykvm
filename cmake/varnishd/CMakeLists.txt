set (VD_PATH ${VARNISH_PATH}/bin/varnishd)

set(VARNISHD_SOURCES
	${VD_PATH}/cache/cache_acceptor.c
	${VD_PATH}/cache/cache_backend.c
	${VD_PATH}/cache/cache_backend_probe.c
	${VD_PATH}/cache/cache_ban.c
	${VD_PATH}/cache/cache_ban_build.c
	${VD_PATH}/cache/cache_ban_lurker.c
	${VD_PATH}/cache/cache_busyobj.c
	${VD_PATH}/cache/cache_cli.c
	${VD_PATH}/cache/cache_deliver_proc.c
	${VD_PATH}/cache/cache_director.c
	${VD_PATH}/cache/cache_esi_deliver.c
	${VD_PATH}/cache/cache_esi_fetch.c
	${VD_PATH}/cache/cache_esi_parse.c
	${VD_PATH}/cache/cache_expire.c
	${VD_PATH}/cache/cache_fetch.c
	${VD_PATH}/cache/cache_fetch_proc.c
	${VD_PATH}/cache/cache_gzip.c
	${VD_PATH}/cache/cache_hash.c
	${VD_PATH}/cache/cache_http.c
	${VD_PATH}/cache/cache_lck.c
	${VD_PATH}/cache/cache_main.c
	${VD_PATH}/cache/cache_mempool.c
	${VD_PATH}/cache/cache_obj.c
	${VD_PATH}/cache/cache_panic.c
	${VD_PATH}/cache/cache_pool.c
	${VD_PATH}/cache/cache_range.c
	${VD_PATH}/cache/cache_req.c
	${VD_PATH}/cache/cache_req_body.c
	${VD_PATH}/cache/cache_req_fsm.c
	${VD_PATH}/cache/cache_rfc2616.c
	${VD_PATH}/cache/cache_session.c
	${VD_PATH}/cache/cache_shmlog.c
	${VD_PATH}/cache/cache_vary.c
	${VD_PATH}/cache/cache_vcl.c
	${VD_PATH}/cache/cache_vrt.c
	${VD_PATH}/cache/cache_vrt_priv.c
	${VD_PATH}/cache/cache_vrt_re.c
	${VD_PATH}/cache/cache_vrt_var.c
	${VD_PATH}/cache/cache_vrt_vmod.c
	${VD_PATH}/cache/cache_wrk.c
	${VD_PATH}/common/common_vsc.c
	${VD_PATH}/common/common_vsmw.c
	${VD_PATH}/hash/hash_classic.c
	${VD_PATH}/hash/hash_critbit.c
	${VD_PATH}/hash/hash_simple_list.c
	${VD_PATH}/hash/mgt_hash.c
	${VD_PATH}/hpack/vhp_decode.c
	${VD_PATH}/hpack/vhp_table.c
	${VD_PATH}/http1/cache_http1_deliver.c
	${VD_PATH}/http1/cache_http1_fetch.c
	${VD_PATH}/http1/cache_http1_fsm.c
	${VD_PATH}/http1/cache_http1_line.c
	${VD_PATH}/http1/cache_http1_pipe.c
	${VD_PATH}/http1/cache_http1_proto.c
	${VD_PATH}/http1/cache_http1_vfp.c
	${VD_PATH}/http2/cache_http2_deliver.c
	${VD_PATH}/http2/cache_http2_hpack.c
	${VD_PATH}/http2/cache_http2_panic.c
	${VD_PATH}/http2/cache_http2_proto.c
	${VD_PATH}/http2/cache_http2_send.c
	${VD_PATH}/http2/cache_http2_session.c
	${VD_PATH}/mgt/mgt_acceptor.c
	${VD_PATH}/mgt/mgt_child.c
	${VD_PATH}/mgt/mgt_cli.c
	${VD_PATH}/mgt/mgt_jail.c
	${VD_PATH}/mgt/mgt_jail_solaris.c
	${VD_PATH}/mgt/mgt_jail_unix.c
	${VD_PATH}/mgt/mgt_main.c
	${VD_PATH}/mgt/mgt_param.c
	${VD_PATH}/mgt/mgt_param_bits.c
	${VD_PATH}/mgt/mgt_param_tbl.c
	${VD_PATH}/mgt/mgt_param_tcp.c
	${VD_PATH}/mgt/mgt_param_tweak.c
	${VD_PATH}/mgt/mgt_shmem.c
	${VD_PATH}/mgt/mgt_util.c
	${VD_PATH}/mgt/mgt_vcc.c
	${VD_PATH}/mgt/mgt_vcl.c
	${VD_PATH}/proxy/cache_proxy_proto.c
	${VD_PATH}/proxy/cache_proxy.h
	${VD_PATH}/storage/mgt_stevedore.c
	${VD_PATH}/storage/stevedore.c
	${VD_PATH}/storage/stevedore_utils.c
	${VD_PATH}/storage/storage_file.c
	${VD_PATH}/storage/storage_lru.c
	${VD_PATH}/storage/storage_malloc.c
	${VD_PATH}/storage/storage_simple.c
	${VD_PATH}/storage/storage_umem.c
	${VD_PATH}/waiter/cache_waiter.c
	${VD_PATH}/waiter/cache_waiter_epoll.c
	${VD_PATH}/waiter/cache_waiter_kqueue.c
	${VD_PATH}/waiter/cache_waiter_poll.c
	${VD_PATH}/waiter/cache_waiter_ports.c
	${VD_PATH}/waiter/mgt_waiter.c
    # persistent storage
    #${VD_PATH}/storage/mgt_storage_persistent.c
	#${VD_PATH}/storage/storage_persistent.c
	#${VD_PATH}/storage/storage_persistent_silo.c
	#{VD_PATH}/storage/storage_persistent_subr.c
    # nodist
	builtin_vcl.c
)
if (VARNISH_PLUS)
    list (APPEND VARNISHD_SOURCES
		${VD_PATH}/cache/cache_accounting.c
		${VD_PATH}/cache/cache_brotli.c
		${VD_PATH}/cache/cache_brotli_vfp.c
		${VD_PATH}/cache/cache_brotli_vdp.c
		${VD_PATH}/cache/cache_governor.c
		${VD_PATH}/cache/cache_seg_deliver.c
		${VD_PATH}/cache/cache_seg_fetch.c
		${VD_PATH}/cache/cache_seg_ykey.c
		${VD_PATH}/cache/cache_tcp_pool.c
		${VD_PATH}/cache/cache_tcp_oper.c
		${VD_PATH}/cache/cache_vcl_vrt.c
		${VD_PATH}/cache/cache_vst.c
		${VD_PATH}/cache/cache_vpi_ykey.c
		${VD_PATH}/cache/cache_ws.c
		${VD_PATH}/cache/cache_ykey.c
		${VD_PATH}/common/common_vtls_sni.c
		${VD_PATH}/mgt/mgt_pool.c
		${VD_PATH}/mgt/mgt_tls_client_cert.c
		${VD_PATH}/storage/storage_ykey_mem.c
		${VD_PATH}/storage/mse_book.c
		${VD_PATH}/storage/mse_cache.c
		${VD_PATH}/storage/mse_obj.c
		${VD_PATH}/storage/mse_memcache.c
		${VD_PATH}/storage/mse_mgt.c
		${VD_PATH}/storage/mse_store.c
		${VD_PATH}/storage/mse_transproxy.c
		${VD_PATH}/storage/mse_aio.c
		${VD_PATH}/storage/mse_panic.c
		${VD_PATH}/storage/mse_populate.c
		${VD_PATH}/storage/mse_varylib.c
		${VD_PATH}/storage/mse_waterlevel.c
		${VD_PATH}/storage/mse_hoic.c
		${VD_PATH}/storage/mse_ykey.c
		${VD_PATH}/tls/cache_backend_ssl.c
		${VD_PATH}/tls/cache_client_ssl.c
		${VD_PATH}/tls/cache_tls.c
		${VD_PATH}/tls/cache_tls_client_cert.c
		${VD_PATH}/vmod_util/brotli_util.c
		${VD_PATH}/vmod_util/crypto_util.c
		${VD_PATH}/vmod_util/edgestash_util.c
		${VD_PATH}/vmod_util/probe_proxy_util.c
		${VD_PATH}/vmod_util/vcp_vfp.c
		${VD_PATH}/vmod_util/vmod_util.c
		${VD_PATH}/vmod_util/waf_util.c
		${VD_PATH}/vmod_util/xbody_util.c
    )
else()
	list (APPEND VARNISHD_SOURCES
		${VD_PATH}/cache/cache_conn_pool.c
        ${VD_PATH}/cache/cache_vrt_filter.c
        ${VD_PATH}/cache/cache_vrt_vcl.c
        ${VD_PATH}/cache/cache_vpi.c
		${VD_PATH}/cache/cache_ws_common.c
		${VD_PATH}/cache/cache_ws.c
		${VD_PATH}/common/common_vext.c
        ${VD_PATH}/mgt/mgt_symtab.c
		${VD_PATH}/storage/storage_debug.c
    )
endif()
if (WORKSPACE_EMULATOR)
	list (REMOVE_ITEM VARNISHD_SOURCES
		${VD_PATH}/cache/cache_ws.c)
	list (APPEND VARNISHD_SOURCES
		${VD_PATH}/cache/cache_ws_emu.c)
endif()

set (LIBPATH ${VARNISH_PATH}/lib)

## HUF DEC generator executable ##
set(GEN_HUFDEC_SOURCES "${VD_PATH}/hpack/vhp_gen_hufdec.c")
add_executable(genhufdec ${GEN_HUFDEC_SOURCES})
target_include_directories(genhufdec PUBLIC ${VARNISH_PATH}/bin/varnishd)
target_include_directories(genhufdec PUBLIC ${VARNISH_PATH}/include)
target_link_libraries(genhufdec varnish)
if (SANITIZE)
    target_link_libraries(genhufdec "${SANITIZE_FLAGS}")
endif()

add_custom_command(
        OUTPUT vhp_hufdec.h
        COMMAND genhufdec > vhp_hufdec.h
        DEPENDS genhufdec
    )
add_custom_target(vhp_gen_hufdec ALL
        DEPENDS vhp_hufdec.h
)

## Generate the built-in VCL C-file ##
add_custom_command(
        OUTPUT builtin_vcl.c
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/builtin_vcl.py ${VD_PATH}/builtin.vcl builtin_vcl.c
        DEPENDS ${VD_PATH}/builtin.vcl
    )
add_custom_target(generate_builtin_vcl
        DEPENDS builtin_vcl.c
)

if (VARNISH_PLUS)
    ## libvarnish_sslhelper library ##
    set(SSLH_PATH ${LIBPATH}/libvarnish_sslhelper)
    set(SSLH_SOURCES ${SSLH_PATH}/vsslh.c
                )
    add_library(sslhelper ${SSLH_SOURCES})
    target_include_directories(sslhelper PRIVATE ${VARNISH_PATH}/include)

    ## LMDB library ##
    set(LMDB_PATH ${LIBPATH}/liblmdb/LMDB_0.9.23)
    set(LMDB_SOURCES
		${LMDB_PATH}/mdb.c
		${LMDB_PATH}/mdb_dump.c
		${LMDB_PATH}/mdb_stat.c
		${LMDB_PATH}/mdb_copy.c
		${LMDB_PATH}/mdb_load.c
		${LMDB_PATH}/midl.c
	)
    add_library(lmdb ${LMDB_SOURCES})
    target_compile_definitions(lmdb PUBLIC _XOPEN_SOURCE=700)
    target_include_directories(lmdb PUBLIC ${LMDB_PATH})
	if (SANITIZE)
		target_compile_options(lmdb PRIVATE "${SANITIZE_FLAGS}")
	endif()
endif()

if (LIBFUZZER)
	set (LIBFUZZER_SOURCES
		fuzz/fuzzer.c
		fuzz/fuzz_sha256.c
		fuzz/fuzz_http.c
		fuzz/fuzz_h2.c
		fuzz/fuzz_proxy.c
		fuzz/fuzz_random.c
		fuzz/fuzz_response.c
		fuzz/fuzz_vmod.c
		fuzz/hpack.c
		fuzz/varnish_http.c
		${THPOOL_PATH}/thpool.c
    )
	set(FUZZER "HTTP" CACHE STRING "Fuzzer mode (HTTP1, HTTP2, PROXY1, PROXY2, ...)")
	message(STATUS "Fuzzing mode is set to: ${FUZZER}")
	# define VARNISH_PLUS for fuzzer sources, when enabled
	if (VARNISH_PLUS)
		list(APPEND LIBFUZZER_SOURCES
			fuzz/fuzz_base64.c
		)
		set_source_files_properties(
			${LIBFUZZER_SOURCES}
		PROPERTIES COMPILE_DEFINITIONS VARNISH_PLUS=1)
	endif()
	# work-around: need to be done after setting VARNISH_PLUS
	set_property(SOURCE ${LIBFUZZER_SOURCES} APPEND
		PROPERTY COMPILE_DEFINITIONS
		FUZZER_${FUZZER}=1
		FUZZING_DIRECTORY="${CMAKE_SOURCE_DIR}"
	)
	list (APPEND VARNISHD_SOURCES ${LIBFUZZER_SOURCES})
endif()

## Varnish daemon ##
add_executable(varnishd ${VARNISHD_SOURCES})
target_include_directories(varnishd PRIVATE ${CMAKE_BINARY_DIR}/cmake/libvarnish)
target_include_directories(varnishd PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if (NUMA_AWARE)
	target_compile_definitions(varnishd PRIVATE
		HAVE_EBPF=1 HAVE_REUSEPORT=1 HAVE_EBPF_NUMA=1)
endif()
add_dependencies(varnishd vhp_gen_hufdec generate_builtin_vcl)
target_link_libraries(varnishd vcc varnish vgz)
target_link_libraries(varnishd config pthread ssl crypto dl)
if (VARNISH_PLUS)
    target_link_libraries(varnishd mse numa lmdb sslhelper jemalloc)
endif()
# build varnishd into the top binary folder
set_target_properties(varnishd
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
    target_compile_options(vcc      PRIVATE "${SANITIZE_FLAGS}")
    target_compile_options(varnishd PUBLIC  "${SANITIZE_FLAGS}")
	set(SANITIZER_FRIENDLY_MODE TRUE)
endif()
if (SANITIZER_FRIENDLY_MODE)
	#target_compile_definitions(vcc      PRIVATE SANITIZER_MODE=1)
	target_compile_definitions(varnishd PUBLIC  SANITIZER_MODE=1)
endif()

if (LIBFUZZER)
    if (NOT "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
        message(FATAL_ERROR "You can only use Clang when libfuzzer is enabled")
    endif()
	target_include_directories(varnishd PRIVATE ${THPOOL_PATH})
    target_compile_options(varnishd PUBLIC ${FUZZER_CFLAGS})
	target_link_libraries(varnishd ${FUZZER_LFLAGS})
    set_source_files_properties(
        ${VD_PATH}/mgt/mgt_child.c
        ${VD_PATH}/mgt/mgt_main.c
		${VD_PATH}/mgt/mgt_param_tbl.c
    PROPERTIES COMPILE_DEFINITIONS LIBFUZZER_ENABLED=1)
	target_compile_definitions(varnishd PRIVATE DONT_DLCLOSE_VMODS=1)
endif()

# libfuzzer is a C++ project, so we need the ability to enable libc++ and LLD
if (LIBCPP)
    target_link_libraries(varnishd "-stdlib=libc++ -fuse-ld=lld")
endif()

if (SINGLE_PROCESS OR LIBFUZZER)
    set_source_files_properties(
		${VD_PATH}/mgt/mgt_child.c
        ${VD_PATH}/mgt/mgt_vcc.c
    PROPERTIES COMPILE_FLAGS -DSINGLE_PROCESS_MODE)
endif()

if (WORKSPACE_EMULATOR)
	target_compile_definitions(varnishd PRIVATE ENABLE_WORKSPACE_EMULATOR=1)
endif()

find_library(LIBUNWIND unwind)

if (LIBUNWIND)
	target_link_libraries(varnishd ${LIBUNWIND})
	target_compile_definitions(varnishd PRIVATE WITH_UNWIND=1 UNW_LOCAL_ONLY=1)
endif()

if (NOT SHARED_LIBRISCV AND VARNISH_PLUS)
	target_link_libraries(varnishd -Wl,--whole-archive sandbox -Wl,--no-whole-archive)
endif()
if (NOT SHARED_KVM AND VARNISH_PLUS)
	target_link_libraries(varnishd -Wl,--whole-archive kvm_sandbox -Wl,--no-whole-archive)
endif()
