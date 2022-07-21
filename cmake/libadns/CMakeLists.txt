set (LIBPATH ${VARNISH_PATH}/lib)
set (VARNISHD_PATH ${VARNISH_PATH}/bin/varnishd)

## async DNS library ##
set (SOURCES
	${LIBPATH}/libadns/adns.c
	${LIBPATH}/libadns/adns_cache.c
	${LIBPATH}/libadns/adns_local_search.c
	${LIBPATH}/libadns/adns_nsswitch.c
	${LIBPATH}/libadns/adns_resolve.c
	${LIBPATH}/libadns/adns_search.c
	${LIBPATH}/libadns/adns_store.c
	${LIBPATH}/libadns/adns_utils.c
	# Giga-hack to compile portions of libvarnish with fPIC
	${LIBPATH}/libvarnish/version.c
	${LIBPATH}/libvarnish/vas.c
	${LIBPATH}/libvarnish/vct.c
	${LIBPATH}/libvarnish/vjsn.c
	${LIBPATH}/libvarnish/vrnd.c
	${LIBPATH}/libvarnish/vsa.c
	${LIBPATH}/libvarnish/vtim.c
)

add_library(adns STATIC ${SOURCES} ${VSC_COMPILED})
target_include_directories(adns PUBLIC ${VARNISH_PATH}/include/adns) # Why?
target_link_libraries(adns PUBLIC vapi_static)
set_property(TARGET adns PROPERTY POSITION_INDEPENDENT_CODE 1)
