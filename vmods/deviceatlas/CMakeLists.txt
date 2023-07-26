cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_deviceatlas)
set (TESTPATH ${VARNISH_SOURCE_DIR}/bin/varnishtest/vmodtests/deviceatlas)

add_vmod(vmod_deviceatlas ${PATH}/vmod_deviceatlas.vcc "DeviceAtlas VMOD"
	${PATH}/vmod_deviceatlas.c
)
target_include_directories(vmod_deviceatlas PRIVATE ${VARNISH_SOURCE_DIR}/lib/deviceatlas-enterprise-c/Src)

add_subdirectory(${VARNISH_SOURCE_DIR}/lib/deviceatlas-enterprise-c da)
target_link_libraries(vmod_deviceatlas da)

# VCL import name is "deviceatlas"
add_vmod_tests(vmod_deviceatlas deviceatlas
	${TESTPATH}/001-load-vmod.vtc
	${TESTPATH}/002-load-datafile.vtc
	${TESTPATH}/003-no-datafile-loaded.vtc
	${TESTPATH}/010-simple-vendor.vtc
	${TESTPATH}/019-invalid-key.vtc
	${TESTPATH}/020-case-insensitive.vtc
	${TESTPATH}/022-integer-response.vtc
	${TESTPATH}/023-boolean-response.vtc
	${TESTPATH}/024-no-useragent.vtc
	${TESTPATH}/025-response-with-whitespace.vtc
	${TESTPATH}/026-lookupint.vtc
	${TESTPATH}/030-manykeys-repeated.vtc
	${TESTPATH}/040-reload-datafile.vtc
	${TESTPATH}/042-init-and-reload-datafile.vtc
	${TESTPATH}/041-repeated-lookups.vtc
	${TESTPATH}/080-allkeys.vtc
)
