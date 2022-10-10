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
)
if (GIGAHACK)
	# Giga-hack to compile portions of libvarnish with fPIC
	list(APPEND
		${LIBPATH}/libvarnish/version.c
		${LIBPATH}/libvarnish/vas.c
		${LIBPATH}/libvarnish/vct.c
		${LIBPATH}/libvarnish/vjsn.c
		${LIBPATH}/libvarnish/vrnd.c
		${LIBPATH}/libvarnish/vsa.c
		${LIBPATH}/libvarnish/vtim.c
	)
endif()

if (VMOD_RELEASE_BUILD)
	# Import from release-installation (globally)
	add_library(adns SHARED IMPORTED GLOBAL)
	set_target_properties(adns PROPERTIES IMPORTED_LOCATION "/usr/lib/varnish-plus/libadns.so")
	target_include_directories(adns INTERFACE ${VARNISH_PATH}/include/adns)
else()
	# Build internally (always global)
	add_library(adns SHARED ${SOURCES} ${VSC_COMPILED})
	target_link_libraries(adns PUBLIC vapi_static resolv)
	set_property(TARGET adns PROPERTY POSITION_INDEPENDENT_CODE 1)
	target_include_directories(adns PUBLIC ${VARNISH_PATH}/include/adns)
endif()
