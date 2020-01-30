cmake_minimum_required (VERSION 3.0.2)
project(mkfsmse C)

set (PATH ${VARNISH_PATH}/bin/mkfs.mse)

set(SOURCES
		${PATH}/mse_mkfs.c
	)
add_executable(mkfsmse ${SOURCES})
target_include_directories(mkfsmse PUBLIC ${VARNISH_PATH}/include)
target_include_directories(mkfsmse PUBLIC ${CONFIG_PATH})
target_compile_definitions(mkfsmse PUBLIC _XOPEN_SOURCE=700)

target_link_libraries(mkfsmse mse lmdb varnish vgz pthread m)

# build varnishtest into the top binary folder
set_target_properties(mkfsmse
	PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
				OUTPUT_NAME "mkfs.mse"
)
