set (PATH ${VARNISH_PATH}/lib/jemalloc)

## jemalloc library ##
set (SOURCES
	${PATH}/jemalloc-builddir/src/arena.c
	${PATH}/jemalloc-builddir/src/atomic.c
	${PATH}/jemalloc-builddir/src/base.c
	${PATH}/jemalloc-builddir/src/bitmap.c
	${PATH}/jemalloc-builddir/src/chunk.c
	${PATH}/jemalloc-builddir/src/chunk_dss.c
	${PATH}/jemalloc-builddir/src/chunk_mmap.c
	${PATH}/jemalloc-builddir/src/ckh.c
	${PATH}/jemalloc-builddir/src/ctl.c
	${PATH}/jemalloc-builddir/src/extent.c
	${PATH}/jemalloc-builddir/src/hash.c
	${PATH}/jemalloc-builddir/src/huge.c
	${PATH}/jemalloc-builddir/src/jemalloc.c
	${PATH}/jemalloc-builddir/src/mb.c
	${PATH}/jemalloc-builddir/src/mutex.c
	${PATH}/jemalloc-builddir/src/prof.c
	${PATH}/jemalloc-builddir/src/quarantine.c
	${PATH}/jemalloc-builddir/src/rtree.c
	${PATH}/jemalloc-builddir/src/stats.c
	${PATH}/jemalloc-builddir/src/tcache.c
	${PATH}/jemalloc-builddir/src/tsd.c
	${PATH}/jemalloc-builddir/src/util.c
)

add_library(jemalloc STATIC ${SOURCES})
target_include_directories(jemalloc PUBLIC ${PATH}/jemalloc-builddir/include)
target_include_directories(jemalloc PRIVATE ${CONFIG_PATH})
target_compile_definitions(jemalloc PRIVATE _GNU_SOURCE)
target_compile_options(jemalloc PRIVATE "-std=gnu11")

if (SANITIZE)
	target_compile_options(jemalloc PRIVATE "${SANITIZE_FLAGS}")
endif()
