cmake_minimum_required (VERSION 3.0.2)
project (varnish C)

# libvcc/... and include/*vcc*
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${VARNISH_PATH}/lib/libvcc/generate.py ${VARNISH_PATH} ${VARNISH_PATH}
    OUTPUT  ${VARNISH_PATH}/lib/libvcc/vcc_fixed_token.c
            ${VARNISH_PATH}/lib/libvcc/vcc_obj.c
            ${VARNISH_PATH}/include/vcl.h
            ${VARNISH_PATH}/include/tbl/vcc_types.h
            ${VARNISH_PATH}/include/tbl/vrt_stv_var.h
    DEPENDS ${VARNISH_PATH}/lib/libvcc/generate.py
    COMMENT "Generating VCL compiler headers and sources"
)
add_custom_target(libvcc_generate
    DEPENDS ${VARNISH_PATH}/lib/libvcc/vcc_fixed_token.c
            ${VARNISH_PATH}/lib/libvcc/vcc_obj.c
            ${VARNISH_PATH}/include/vcl.h
            ${VARNISH_PATH}/include/tbl/vcc_types.h
            ${VARNISH_PATH}/include/tbl/vrt_stv_var.h
            includes_generate
    )
#set_source_files_properties(${VARNISH_PATH}/lib/libvcc/vcc_fixed_token.c
#                            PROPERTIES GENERATED TRUE)

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
	${VD_PATH}/cache/cache_tcp_pool.c
	${VD_PATH}/cache/cache_vary.c
	${VD_PATH}/cache/cache_vcl.c
	${VD_PATH}/cache/cache_vrt.c
	${VD_PATH}/cache/cache_vrt_priv.c
	${VD_PATH}/cache/cache_vrt_re.c
	${VD_PATH}/cache/cache_vrt_var.c
	${VD_PATH}/cache/cache_vrt_vmod.c
	${VD_PATH}/cache/cache_wrk.c
	${VD_PATH}/cache/cache_ws.c
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
	${VD_PATH}/mgt/mgt_pool.c
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
    ${VD_PATH}/storage/mgt_storage_persistent.c
	${VD_PATH}/storage/storage_persistent.c
	${VD_PATH}/storage/storage_persistent_silo.c
	${VD_PATH}/storage/storage_persistent_subr.c
    # nodist
	builtin_vcl.c
)
if (VARNISH_PLUS)
    list (APPEND VARNISHD_SOURCES
		${VD_PATH}/cache/cache_backend_ssl.c
		${VD_PATH}/cache/cache_tcp_oper.c
		${VD_PATH}/cache/cache_vcl_vrt.c
		${VD_PATH}/cache/cache_vrt_ykey.c
		${VD_PATH}/cache/cache_vst_access.c
		${VD_PATH}/cache/cache_ykey.c
		${VD_PATH}/storage/storage_ykey_mem.c
		${VD_PATH}/storage/mse_mgt.c
		${VD_PATH}/storage/mse_cache.c
		${VD_PATH}/storage/mse_obj.c
		${VD_PATH}/storage/mse_memcache.c
		${VD_PATH}/storage/mse_store.c
		${VD_PATH}/storage/mse_aio.c
		${VD_PATH}/storage/mse_populate.c
		${VD_PATH}/storage/mse_varylib.c
		${VD_PATH}/storage/mse_waterlevel.c
		${VD_PATH}/storage/mse_hoic.c
		${VD_PATH}/storage/mse_ykey.c
		${VD_PATH}/vmod_util/crypto_util.c
		${VD_PATH}/vmod_util/edgestash_util.c
		${VD_PATH}/vmod_util/file_util.c
		${VD_PATH}/vmod_util/synthbackend_util.c
		${VD_PATH}/vmod_util/vmod_util.c
		${VD_PATH}/vmod_util/waf_util.c
		${VD_PATH}/vmod_util/xbody_util.c
    )
else()
	list (APPEND VARNISHD_SOURCES
        ${VD_PATH}/cache/cache_vrt_filter.c
        ${VD_PATH}/cache/cache_vrt_vcl.c
        ${VD_PATH}/cache/cache_vpi.c
        ${VD_PATH}/mgt/mgt_symtab.c
    )
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
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/builtin_vcl.py ${VD_PATH}/builtin.vcl builtin_vcl.c
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
    set(LMDB_SOURCES ${LMDB_PATH}/mdb.c
                     ${LMDB_PATH}/mdb_dump.c
                     ${LMDB_PATH}/mdb_stat.c
                     ${LMDB_PATH}/mdb_copy.c
                     ${LMDB_PATH}/mdb_load.c
                     ${LMDB_PATH}/midl.c
                )
    add_library(lmdb ${LMDB_SOURCES})
    target_compile_definitions(lmdb PUBLIC _XOPEN_SOURCE=700)
    target_include_directories(lmdb PUBLIC ${LMDB_PATH})

    ## MSE library ##
    set(MSE_PATH ${LIBPATH}/libmse)
    set(MSE_SOURCES ${MSE_PATH}/journal_test.c
                    ${MSE_PATH}/mse_lib_config_test.c
                    ${MSE_PATH}/mse_lib_dbtest.c
                    ${MSE_PATH}/mse_lib_jrnfile.c
                    ${MSE_PATH}/mse_lib_banlist.c
                    ${MSE_PATH}/mse_lib_db.c
                    ${MSE_PATH}/mse_lib_env.c
                    ${MSE_PATH}/mse_mtq_test.c
                    ${MSE_PATH}/mse_lib_config.c
                    ${MSE_PATH}/mse_lib_dbro.c
                    ${MSE_PATH}/mse_lib_journal.c
                )
    add_library(mse ${MSE_SOURCES})
    target_include_directories(mse PRIVATE ${MSE_PATH})
    target_include_directories(mse PRIVATE ${LIBPATH}/libvgz)
    target_include_directories(mse PRIVATE ${LIBPATH}/liblmdb/LMDB_0.9.23)
    target_include_directories(mse PRIVATE ${VARNISH_PATH}/include)
    target_include_directories(mse PRIVATE ${CONFIG_PATH})
	# we need to link with libconfig
	target_link_libraries(mse config)
endif()

## VCC library ##
set(VCC_PATH ${LIBPATH}/libvcc)
set(VCC_SOURCES ${VCC_PATH}/vcc_acl.c
                ${VCC_PATH}/vcc_backend_util.c
                ${VCC_PATH}/vcc_fixed_token.c
                ${VCC_PATH}/vcc_storage.c
                ${VCC_PATH}/vcc_types.c
                ${VCC_PATH}/vcc_vmod.c
                ${VCC_PATH}/vcc_action.c
                ${VCC_PATH}/vcc_compile.c
                ${VCC_PATH}/vcc_obj.c
                ${VCC_PATH}/vcc_symb.c
                ${VCC_PATH}/vcc_utils.c
                ${VCC_PATH}/vcc_xref.c
                ${VCC_PATH}/vcc_backend.c
                ${VCC_PATH}/vcc_expr.c
                ${VCC_PATH}/vcc_parse.c
                ${VCC_PATH}/vcc_token.c
                ${VCC_PATH}/vcc_var.c
            )
add_library(vcc ${VCC_SOURCES})
target_include_directories(vcc PRIVATE ${VCC_PATH})
target_include_directories(vcc PRIVATE ${VARNISH_PATH}/include)
target_include_directories(vcc PRIVATE ${CONFIG_PATH})
add_dependencies(vcc libvcc_generate)

if (LIBFUZZER)
	set (LIBFUZZER_SOURCES
		fuzz/fuzzer.c
		fuzz/fuzz_http.c
		fuzz/fuzz_h2.c
		fuzz/fuzz_proxy.c
		fuzz/fuzz_response.c
		fuzz/hpack.c
		fuzz/varnish_http.c
    )
	if (NOT FUZZER)
		message(FATAL_ERROR "When a fuzzer is enabled, select fuzzing method with -DFUZZER=...")
	endif()
	# define VARNISH_PLUS for fuzzer sources, when enabled
	if (VARNISH_PLUS)
		set_source_files_properties(
			${LIBFUZZER_SOURCES}
		PROPERTIES COMPILE_DEFINITIONS VARNISH_PLUS=1)
	endif()
	# work-around: need to be done after setting VARNISH_PLUS
	set_property(SOURCE fuzz/fuzzer.c APPEND
		PROPERTY COMPILE_DEFINITIONS FUZZER_${FUZZER}=1)

	list (APPEND VARNISHD_SOURCES ${LIBFUZZER_SOURCES})
endif()

## Varnish daemon ##
add_executable(varnishd ${VARNISHD_SOURCES})
target_include_directories(varnishd PRIVATE ${CMAKE_BINARY_DIR}/libvarnish)
target_include_directories(varnishd PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(varnishd vhp_gen_hufdec generate_builtin_vcl)
target_link_libraries(varnishd vcc varnish vgz m)
target_link_libraries(varnishd config pcre pthread crypto ssl dl)
if (VARNISH_PLUS)
    target_link_libraries(varnishd mse lmdb sslhelper)
endif()
# build varnishd into the top binary folder
set_target_properties(varnishd
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if (SANITIZE)
    target_compile_options(vcc PRIVATE     "${SANITIZE_FLAGS}")
    target_compile_options(varnishd PUBLIC "${SANITIZE_FLAGS}")
endif()

if (LIBFUZZER)
    if (NOT "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
        message(FATAL_ERROR "You can only use Clang when libfuzzer is enabled")
    endif()
    target_compile_options(varnishd PUBLIC ${FUZZER_CFLAGS})
	target_link_libraries(varnishd ${FUZZER_LFLAGS})
    set_source_files_properties(
        ${VD_PATH}/mgt/mgt_child.c
        ${VD_PATH}/mgt/mgt_main.c
    PROPERTIES COMPILE_DEFINITIONS LIBFUZZER_ENABLED=1)
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
