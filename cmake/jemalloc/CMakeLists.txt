set (JEMALLOC_VERSION "3.6.0")
set (PATH "${CMAKE_CURRENT_BINARY_DIR}/jemalloc-${JEMALLOC_VERSION}")

set (JEMALLOC_ARCHIVE  "${VARNISH_PATH}/lib/jemalloc/jemalloc-${JEMALLOC_VERSION}.tar.gz")

## unpack jemalloc release tarball ##
add_custom_command(
	COMMAND ${CMAKE_COMMAND} -E tar xzf ${JEMALLOC_ARCHIVE}
	OUTPUT  ${PATH}/src/arena.c
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	DEPENDS ${JEMALLOC_ARCHIVE}
	COMMENT "Unpacking ${JEMALLOC_ARCHIVE}"
	VERBATIM)
add_custom_target(jemalloc_in_a_box
	DEPENDS ${PATH}/src/arena.c)

## configure jemalloc to generate missing headers ##
add_custom_command(
	COMMAND bash ./configure
	OUTPUT  ${PATH}/include/jemalloc/jemalloc.h
	WORKING_DIRECTORY ${PATH}
	DEPENDS jemalloc_in_a_box
	VERBATIM)
add_custom_target(jemalloc_configure
	DEPENDS ${PATH}/include/jemalloc/jemalloc.h)

## jemalloc library ##
set (SOURCES
	${PATH}/src/arena.c
	${PATH}/src/atomic.c
	${PATH}/src/base.c
	${PATH}/src/bitmap.c
	${PATH}/src/chunk.c
	${PATH}/src/chunk_dss.c
	${PATH}/src/chunk_mmap.c
	${PATH}/src/ckh.c
	${PATH}/src/ctl.c
	${PATH}/src/extent.c
	${PATH}/src/hash.c
	${PATH}/src/huge.c
	${PATH}/src/jemalloc.c
	${PATH}/src/mb.c
	${PATH}/src/mutex.c
	${PATH}/src/prof.c
	${PATH}/src/quarantine.c
	${PATH}/src/rtree.c
	${PATH}/src/stats.c
	${PATH}/src/tcache.c
	${PATH}/src/tsd.c
	${PATH}/src/util.c
)
set_source_files_properties(${SOURCES} PROPERTIES GENERATED TRUE)

if (SANITIZE)
	add_library(jemalloc SHARED ${SOURCES})
	target_compile_options(jemalloc PRIVATE "${SANITIZE_FLAGS}")
else()
	add_library(jemalloc STATIC ${SOURCES})
endif()
target_include_directories(jemalloc PUBLIC ${PATH}/include)
target_include_directories(jemalloc PRIVATE ${CONFIG_PATH})
target_compile_definitions(jemalloc PRIVATE _GNU_SOURCE)
target_compile_options(jemalloc PRIVATE "-std=gnu11" "-O3")
if (NATIVE)
	target_compile_options(jemalloc PRIVATE "-march=native")
endif()
target_link_libraries(jemalloc pthread)
add_dependencies(jemalloc jemalloc_configure)
