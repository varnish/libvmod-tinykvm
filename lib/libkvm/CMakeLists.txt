cmake_minimum_required (VERSION 3.5)
project (kvm_sandbox C CXX)

option(SHARED_KVM "Build the KVM sandbox PIC" ON)

# WARNING: everything!
set(CMAKE_C_FLAGS "-Wall -Wextra -std=c11 -O2 -g")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++17 -O2 -g")

set(KVM_SOURCES
	adns.cpp
	archive.cpp
	backend.cpp
	epoll_vm.cpp
	kvm_settings.cpp
	kvm_vcc_api.cpp
	live_update.cpp
	long_lived.cpp
	machine_debug.cpp
	machine_instance.cpp
	program_instance.cpp
	system_calls.cpp
	tenant.cpp
	tenant_instance.cpp
	ext/tinykvm/src/functions.cpp  # setup_kvm_system_calls
	utils/crc32.cpp
	varnish_interface.c
	### cURL ###
	curl_fetch.cpp
)

include(FetchContent)
FetchContent_Declare(
  concurrentqueue
  GIT_REPOSITORY    https://github.com/cameron314/concurrentqueue
  GIT_TAG           master
)
FetchContent_MakeAvailable(concurrentqueue)

add_subdirectory(ext/tinykvm/lib  tinykvm)
add_subdirectory(ext/json         json)

add_library(kvm STATIC ${KVM_SOURCES})
target_include_directories(kvm PRIVATE ../../src)
target_link_libraries(kvm PRIVATE tinykvm curl concurrentqueue nlohmann_json archive)

if (SHARED_KVM)
	set_property(TARGET tinykvm PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET kvm PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()

# Varnish VMOD stuff
find_package(PkgConfig)
pkg_check_modules(Varnish REQUIRED varnishapi)
target_link_libraries(kvm PRIVATE ${VARNISH_LIBRARIES})
target_include_directories(kvm PUBLIC ${VARNISH_INCLUDE_DIRS})
target_include_directories(kvm PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ActiveDNS
target_link_libraries(kvm PRIVATE adns)
target_compile_definitions(kvm PUBLIC VMOD_EVENT_ENABLED=1)
