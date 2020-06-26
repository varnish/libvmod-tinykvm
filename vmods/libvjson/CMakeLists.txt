set (PATH ${VARNISH_SOURCE_DIR}/lib/libedgestash)

if (VARNISH_PLUS)
	set(VCP_DEFINE "#define VARNISH_PLUS 1")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.h
	"${VCP_DEFINE}\n"
)

set(SOURCES
	${PATH}/ves_json.c
	${PATH}/ves_json_nav.c
	${PATH}/ves_json_util.c
)
if (NOT VARNISH_PLUS)
	list(APPEND SOURCES ${PATH}/ves_json_nav.c)
endif()

add_library(vjson SHARED ${SOURCES})
target_include_directories(vjson PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/include/edgestash)
target_include_directories(vjson PRIVATE ${VARNISH_SOURCE_DIR}/bin/varnishd)
