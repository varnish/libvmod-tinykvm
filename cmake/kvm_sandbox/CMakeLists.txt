cmake_minimum_required (VERSION 3.0.2)
project (kvm_sandbox CXX)

option(SHARED_KVM "Build the KVM sandbox PIC" ON)

# WARNING: everything!
set(CMAKE_CXX_FLAGS "-Wall -Wextra -std=c++20 -O2 -g")

set(KVM_SOURCES
	src/backend.cpp
	src/kvm_vcc_api.cpp
	src/live_update.cpp
	src/machine_debug.cpp
	src/machine_instance.cpp
	src/program_instance.cpp
	src/system_calls.cpp
	src/tenant.cpp
	src/tenant_dfm.cpp
	src/tenant_instance.cpp
	tinykvm/src/functions.cpp  # setup_kvm_system_calls
	src/varnish_interface.c
	### VMODS ###
	src/vmods/vmods.cpp
	src/vmods/goto.cpp
)

add_subdirectory(tinykvm/lib tinykvm)
if (NATIVE)
	target_compile_options(tinykvm PUBLIC -march=native -Ofast -fno-fast-math)
endif()
if (LTO_ENABLE)
	target_compile_options(tinykvm PUBLIC -flto)
endif()

add_library(kvm_sandbox STATIC ${KVM_SOURCES})
set_target_properties(kvm_sandbox PROPERTIES CXX_STANDARD 17)
target_include_directories(kvm_sandbox PRIVATE ${VARNISH_PATH}/include)
target_include_directories(kvm_sandbox PRIVATE ${VARNISH_PATH}/bin/varnishd)
# We want to see the stdout/stderr of guest programs (for now)
target_compile_definitions(kvm_sandbox PRIVATE ENABLE_GUEST_STDOUT=1)
target_link_libraries(kvm_sandbox PRIVATE tinykvm nlohmann_json stdc++)

if (SHARED_KVM)
	set_property(TARGET tinykvm PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET kvm_sandbox PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
