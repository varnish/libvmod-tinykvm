cmake_minimum_required (VERSION 3.0.2)
project (varnish C)

set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -g -O2")

option(GPROF       "Enable profiling with gprof" OFF)
option(LTO_ENABLE  "Enable LTO builds" ON)
option(MINIMAL     "Minimal build")
option(GCSECTIONS  "Garbage collect empty sections" ON)
option(NATIVE      "Enable native performance" ON)
option(SANITIZE    "Enable asan/ubsan sanitizers" OFF)
option(PGO_ENABLE  "Enable profile-guided optimizations" OFF)
option(PGO_GENERATE "Enable profile-guided optimizations" ON)
option(AUTOFDO     "Enable AutoFDO profile optimizations" OFF)
set(PROFILE "../perf.afdo" CACHE STRING "Optimization profile path")
option(LIBFUZZER   "Enable fuzzing with clang libfuzzer")
option(SINGLE_PROCESS "Disable fork() and run in single-process mode" ON)

if (LTO_ENABLE)
    if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=thin")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin -fuse-ld=lld")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=8")
    endif()
endif()

if (GPROF)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
endif()

if (MINIMAL)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
endif()
if (GCSECTIONS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-gc-sections")
endif()

if (NATIVE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
endif()

if (SANITIZE)
    set(SANITIZE_FLAGS "-fsanitize=address,undefined")
endif()

if (PGO_ENABLE)
    set(PGO_DIR "${CMAKE_BINARY_DIR}/pgo")
    set(PGO_FLAGS "-fprofile-correction -fprofile-dir=${PGO_DIR}")
    if (PGO_GENERATE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-generate ${PGO_FLAGS}")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-use ${PGO_FLAGS}")
    endif()
endif()

if (AUTOFDO)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fauto-profile=${CMAKE_BINARY_DIR}/${PROFILE} -g1")
endif()

set (VARNISH_PATH ${CMAKE_SOURCE_DIR}/varnish-cache-plus)

# include/vcs_version.h include/vmod_abi.h
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${VARNISH_PATH}/include/generate.py ${VARNISH_PATH} ${VARNISH_PATH}
    WORKING_DIRECTORY ${VARNISH_PATH}/include
    OUTPUT  ${VARNISH_PATH}/include/vcs_version.h
            ${VARNISH_PATH}/include/vmod_abi.h
    DEPENDS ${VARNISH_PATH}/include/generate.py
    COMMENT "Generating vcs_version.h and vmod_abi.h"
    )
add_custom_target(includes_generate
    DEPENDS ${VARNISH_PATH}/include/vcs_version.h
            ${VARNISH_PATH}/include/vmod_abi.h
    )
# libvarnishapi/vxp_fixed_token.c
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${VARNISH_PATH}/lib/libvarnishapi/generate.py
    WORKING_DIRECTORY ${VARNISH_PATH}/lib/libvarnishapi
    OUTPUT  ${VARNISH_PATH}/lib/libvarnishapi/vxp_fixed_token.c
    DEPENDS ${VARNISH_PATH}/lib/libvarnishapi/generate.py
    COMMENT "Generating vxp_fixed_token.c"
)
add_custom_target(varnishapi_generate
    DEPENDS ${VARNISH_PATH}/lib/libvarnishapi/vxp_fixed_token.c
            includes_generate
    )
# libvcc/... and include/*vcc*
add_custom_command(
    COMMAND ${PYTHON_EXECUTABLE} ${VARNISH_PATH}/lib/libvcc/generate.py ${VARNISH_PATH} ${VARNISH_PATH}
    OUTPUT  ${VARNISH_PATH}/lib/libvcc/vcc_fixed_token.c
            ${VARNISH_PATH}/lib/libvcc/vcc_obj.c
            ${VARNISH_PATH}/include/vcl.h
            ${VARNISH_PATH}/include/tbl/vcc_types.h
            ${VARNISH_PATH}/include/tbl/vrt_stv_var.h
    DEPENDS ${VARNISH_PATH}/lib/libvcc/generate.py
    COMMENT "Generating "
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
	${VD_PATH}/cache/cache_backend_ssl.c
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
    ${VD_PATH}/cache/cache_tcp_oper.c
	${VD_PATH}/cache/cache_vary.c
	${VD_PATH}/cache/cache_vcl.c
	${VD_PATH}/cache/cache_vcl_vrt.c
	${VD_PATH}/cache/cache_vrt.c
	${VD_PATH}/cache/cache_vrt_priv.c
	${VD_PATH}/cache/cache_vrt_re.c
	${VD_PATH}/cache/cache_vrt_var.c
	${VD_PATH}/cache/cache_vrt_vmod.c
	${VD_PATH}/cache/cache_vrt_ykey.c
	${VD_PATH}/cache/cache_wrk.c
	${VD_PATH}/cache/cache_ws.c
	${VD_PATH}/cache/cache_ykey.c
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
	${VD_PATH}/vmod_util/xbody_util.c
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
    ${CMAKE_SOURCE_DIR}/vcl/builtin_vcl.c
)
set (LIBPATH ${VARNISH_PATH}/lib)
set (LIBVARNISH_SOURCES
  ${LIBPATH}/libvarnish/binary_heap.c
  ${LIBPATH}/libvarnish/vas.c
  ${LIBPATH}/libvarnish/vav.c
  ${LIBPATH}/libvarnish/vcli_proto.c
  ${LIBPATH}/libvarnish/vcli_serve.c
  ${LIBPATH}/libvarnish/vct.c
  ${LIBPATH}/libvarnish/version.c
  ${LIBPATH}/libvarnish/vev.c
  ${LIBPATH}/libvarnish/vfil.c
  ${LIBPATH}/libvarnish/vfl.c
  ${LIBPATH}/libvarnish/vin.c
  ${LIBPATH}/libvarnish/vjsn.c
  ${LIBPATH}/libvarnish/vlu.c
  ${LIBPATH}/libvarnish/vmb.c
  ${LIBPATH}/libvarnish/vnum.c
  ${LIBPATH}/libvarnish/vpf.c
  ${LIBPATH}/libvarnish/vre.c
  ${LIBPATH}/libvarnish/vrnd.c
  ${LIBPATH}/libvarnish/vsa.c
  ${LIBPATH}/libvarnish/vsb.c
  ${LIBPATH}/libvarnish/vsha256.c
  ${LIBPATH}/libvarnish/vss.c
  ${LIBPATH}/libvarnish/vsub.c
  ${LIBPATH}/libvarnish/vtcp.c
  ${LIBPATH}/libvarnish/vtim.c
  ${LIBPATH}/libvarnish/vus.c
)

## VSC generation ##
set(VSC_SOURCES
	VSC_lck.vsc
	VSC_main.vsc
	VSC_mempool.vsc
	VSC_mgt.vsc
	VSC_sma.vsc
	VSC_smf.vsc
	VSC_smu.vsc
	VSC_vbe.vsc
	VSC_mse.vsc
	VSC_mse_book.vsc
	VSC_mse_store.vsc
)
function(build_vsc_file NAME)
    get_filename_component(VSC_FILE ${NAME} NAME_WE)
    add_custom_command(
            OUTPUT ${VSC_FILE}.c ${VSC_FILE}.h
            COMMAND python3 ${LIBPATH}/libvcc/vsctool.py -ch ${NAME}
            DEPENDS ${NAME}
        )
endfunction()
foreach(VSC_FILE ${VSC_SOURCES})
    build_vsc_file(${VD_PATH}/${VSC_FILE})
    get_filename_component(VSC_WEFILE ${VSC_FILE} NAME_WE)
    list(APPEND VSC_COMPILED ${CMAKE_CURRENT_BINARY_DIR}/${VSC_WEFILE}.c)
    list(APPEND VSC_HEADERS ${CMAKE_CURRENT_BINARY_DIR}/${VSC_WEFILE}.h)
endforeach()
set_source_files_properties(${VSC_COMPILED} PROPERTIES GENERATED TRUE)
set_source_files_properties(${VSC_HEADERS} PROPERTIES GENERATED TRUE)

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

## libvarnish library ##
add_library(varnish ${LIBVARNISH_SOURCES} ${VSC_COMPILED})
target_include_directories(varnish PUBLIC ${VARNISH_PATH}/bin/varnishd)
target_include_directories(varnish PUBLIC ${VARNISH_PATH}/include)
target_include_directories(varnish PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(varnish PUBLIC vcl/)
target_compile_definitions(varnish PUBLIC _XOPEN_SOURCE=700)
target_compile_definitions(varnish PUBLIC VARNISH_STATE_DIR="/tmp/varnish")
target_compile_definitions(varnish PUBLIC VARNISH_VMOD_DIR="/opt/varnish/lib/varnish-plus/vmods")
target_compile_definitions(varnish PUBLIC VARNISH_VCL_DIR="/opt/varnish/share/varnish-plus/vcl")
target_compile_definitions(varnish PUBLIC -DNOT_IN_A_VMOD)
add_dependencies(varnish includes_generate)

## libvarnish_sslhelper library ##
set(SSLH_PATH ${LIBPATH}/libvarnish_sslhelper)
set(SSLH_SOURCES ${SSLH_PATH}/vsslh.c
            )
add_library(sslhelper ${SSLH_SOURCES})
target_include_directories(sslhelper PRIVATE ${VARNISH_PATH}/include)

## MSE library ##
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
target_include_directories(mse PRIVATE vcl/)

## VGZ library ##
set(VGZ_PATH ${LIBPATH}/libvgz)
set(VGZ_SOURCES ${VGZ_PATH}/adler32.c
                ${VGZ_PATH}/crc32.c
                ${VGZ_PATH}/deflate.c
                ${VGZ_PATH}/inffast.c
                ${VGZ_PATH}/inflate.c
                ${VGZ_PATH}/inftrees.c
                ${VGZ_PATH}/trees.c
                ${VGZ_PATH}/zutil.c
            )
add_library(vgz ${VGZ_SOURCES})
target_include_directories(vgz PUBLIC ${VGZ_PATH})

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
target_include_directories(vcc PRIVATE vcl/)
add_dependencies(vcc libvcc_generate)

## missing functions ##
set(VARNISHD_SOURCES ${VARNISHD_SOURCES} missing.c)
if (LIBFUZZER)
    set(VARNISHD_SOURCES ${VARNISHD_SOURCES}
        fuzz/fuzzer.c
        fuzz/fuzz_http.c
    )
endif()

## Varnish daemon ##
add_executable(varnishd ${VARNISHD_SOURCES})
add_dependencies(varnishd vhp_gen_hufdec)
target_link_libraries(varnishd mse vcc sslhelper varnish vgz lmdb m)
target_link_libraries(varnishd config pcre pthread crypto ssl dl)

if (LIBFUZZER)
    if (NOT "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
        message(FATAL_ERROR "You can only use Clang when libfuzzer is enabled")
    endif()
    target_compile_options(varnishd PUBLIC "-fsanitize=fuzzer-no-link")
    target_link_libraries(varnishd "-fsanitize=fuzzer")
    set_source_files_properties(
        ${VD_PATH}/cache/cache_cli.c
        ${VD_PATH}/cache/cache_main.c
        ${VD_PATH}/cache/cache_panic.c
        ${VD_PATH}/mgt/mgt_child.c
        ${VD_PATH}/mgt/mgt_main.c
        ${VD_PATH}/mgt/mgt_vcc.c
        fuzz/fuzzer.c
        fuzz/fuzz_http.c
    PROPERTIES COMPILE_DEFINITIONS LIBFUZZER_ENABLED=1)
endif()

if (SINGLE_PROCESS)
    set_source_files_properties(
        ${VD_PATH}/cache/cache_cli.c
        ${VD_PATH}/cache/cache_vcl.c
        ${VD_PATH}/mgt/mgt_child.c
        ${VD_PATH}/mgt/mgt_vcl.c
    PROPERTIES COMPILE_FLAGS -DSINGLE_PROCESS_MODE)
endif()

if (SANITIZE)
    target_compile_options(vcc PRIVATE     "${SANITIZE_FLAGS}")
    target_compile_options(varnish PRIVATE "${SANITIZE_FLAGS}")
    target_compile_options(varnishd PUBLIC "${SANITIZE_FLAGS}")
    target_link_libraries(varnishd "${SANITIZE_FLAGS}")
endif()
