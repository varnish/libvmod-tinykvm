cmake_minimum_required (VERSION 3.0.2)
project (kvm_sandbox CXX)

option(SHARED_KVM "Build the KVM sandbox PIC" ON)
option(KVM_FACEDETECTION "" OFF)

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
	src/utils/crc32.cpp
	src/varnish_interface.c
	### VMODS ###
	src/vmods/vmods.cpp
	src/vmods/goto.cpp
	src/vmods/http.cpp
)
if (KVM_FACEDETECTION)
	list(APPEND KVM_SOURCES
		src/vmods/facedetect.cpp
	)
endif()


include(FetchContent)
FetchContent_Declare(
  concurrentqueue
  GIT_REPOSITORY    https://github.com/cameron314/concurrentqueue
  GIT_TAG           master
)
FetchContent_MakeAvailable(concurrentqueue)

add_subdirectory(tinykvm/lib tinykvm)
if (NATIVE)
	target_compile_options(tinykvm PUBLIC -march=native -Ofast -fno-fast-math)
endif()
if (LTO_ENABLE)
	target_compile_options(tinykvm PUBLIC -flto)
endif()

add_library(kvm_sandbox STATIC ${KVM_SOURCES})
target_include_directories(kvm_sandbox PRIVATE ${VARNISH_PATH}/include)
target_include_directories(kvm_sandbox PRIVATE ${VARNISH_PATH}/bin/varnishd)
target_link_libraries(kvm_sandbox PRIVATE tinykvm concurrentqueue nlohmann_json stdc++)

if (SHARED_KVM)
	set_property(TARGET tinykvm PROPERTY POSITION_INDEPENDENT_CODE 1)
	set_property(TARGET kvm_sandbox PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
if (DEBUGGING)
    target_compile_options(tinykvm PUBLIC -O0 -ggdb3)
	target_compile_options(kvm_sandbox PUBLIC -O0 -ggdb3)
endif()
if (SANITIZE)
    target_compile_options(tinykvm PUBLIC  "${SANITIZE_FLAGS}")
	target_compile_options(kvm_sandbox PUBLIC "${SANITIZE_FLAGS}")
	target_link_libraries(kvm_sandbox PRIVATE ubsan)
endif()
if (KVM_FACEDETECTION)
	set(FACEDETECT_FOLDER "$ENV{HOME}/git/libfacedetection")
	find_package(OpenCV REQUIRED)
    include_directories(${OpenCV_INCLUDE_DIRS})

	add_library(facedetect STATIC IMPORTED)
	target_include_directories(facedetect INTERFACE ${FACEDETECT_FOLDER}/src)
	target_include_directories(facedetect INTERFACE ${FACEDETECT_FOLDER}/build)
	set_property(TARGET facedetect PROPERTY IMPORTED_LOCATION
		"${FACEDETECT_FOLDER}/build/libfacedetection.a")

	target_link_libraries(kvm_sandbox PUBLIC facedetect ${OpenCV_LIBS})
	target_compile_definitions(kvm_sandbox PUBLIC KVM_FACEDETECTION=1)
endif()
