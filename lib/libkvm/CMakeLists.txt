cmake_minimum_required (VERSION 3.5)
project (kvm_sandbox C CXX)

option(SHARED_KVM "Build the KVM sandbox PIC" ON)

# WARNING: everything!
set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -O2 -g")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++17 -O2 -g")

set(KVM_SOURCES
	archive.cpp
	backend.cpp
	kvm_settings.cpp
	kvm_stats.cpp
	kvm_vcc_api.cpp
	live_update.cpp
	machine_debug.cpp
	machine_instance.cpp
	program_instance.cpp
	system_calls.cpp
	tenant.cpp
	tenant_instance.cpp
	server/epoll.cpp
	server/websocket.cpp
	ext/tinykvm/src/functions.cpp  # setup_kvm_system_calls
	utils/crc32.cpp
	varnish_interface.c
	### cURL ###
	curl_fetch.cpp
)

add_subdirectory(ext/tinykvm/lib     tinykvm)
add_subdirectory(ext/json            json)
add_subdirectory(ext/concurrentqueue concurrentqueue)

# Boost::beast is used for the websocket server
#include(FindBoost)
#find_package(Boost 1.74 REQUIRED COMPONENTS system thread)

add_library(kvm STATIC ${KVM_SOURCES})
target_include_directories(kvm PRIVATE ../../src/tinykvm ../../src/kvm)
if (VARNISH_PLUS)
	target_compile_definitions(kvm PUBLIC VARNISH_PLUS=1)
endif()
target_link_libraries(kvm PRIVATE tinykvm curl concurrentqueue nlohmann_json archive)
#target_link_libraries(kvm PRIVATE Boost::system Boost::thread)

if (SHARED_KVM)
	set_property(TARGET tinykvm PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET kvm PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()

# Varnish VMOD stuff
find_package(PkgConfig)
pkg_check_modules(VAPI REQUIRED varnishapi)
target_link_libraries(kvm PRIVATE ${VAPI_LIBRARIES})
target_include_directories(kvm PUBLIC ${VAPI_INCLUDE_DIRS})
target_compile_options(kvm PUBLIC ${VAPI_CFLAGS_OTHER})
target_include_directories(kvm PUBLIC ${CMAKE_SOURCE_DIR}/include)
