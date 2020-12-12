set (PATH ${VARNISH_SOURCE_DIR}/lib/libedgestash)

set(SOURCES
	${PATH}/ves_json.c
	${PATH}/ves_json_nav.c
	${PATH}/ves_json_util.c
)
if (NOT VARNISH_PLUS)
	list(APPEND SOURCES ${PATH}/ves_json_nav.c)
endif()

set(VMOD_CMAKE_PATH ${CMAKE_CURRENT_LIST_DIR}/..)


add_library(vjson SHARED ${SOURCES})
target_include_directories(vjson PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(vjson PRIVATE ${VMOD_CMAKE_PATH}/include)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include/edgestash)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
if (VARNISH_PLUS)
	target_compile_definitions(vjson PRIVATE VARNISH_PLUS=1)
endif()
