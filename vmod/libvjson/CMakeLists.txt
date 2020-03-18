set (PATH ${VARNISH_SOURCE_DIR}/lib/libedgestash)

set(SOURCES
	${PATH}/ves_json.c
	${PATH}/ves_json_util.c
)

add_library(vjson SHARED ${SOURCES})
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR})
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include/edgestash)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
