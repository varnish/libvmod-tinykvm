set (LIBPATH ${VARNISH_PATH}/lib)

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

if (VARNISH_PLUS)
    target_compile_definitions(vgz PUBLIC z_off_t=long)
endif()
