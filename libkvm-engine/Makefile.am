## Builds the bundled libkvm sandbox (lib/libkvm) and its tinykvm hypervisor
## dependency as static archives that the VMODs link into libvmod_kvm.so and
## libvmod_tinykvm.so.
##
## This mirrors how libvmod-riscv builds its engine (libriscv-engine) out of
## band: the engine keeps its own (CMake) build system and we drive it from an
## all-local hook, then the VMODs link the resulting archives. automake and
## cmake ignore each other; the (untouched) CMakeLists.txt files still work for
## the standalone CMake developer build.
##
## The kvm CMake target depends on tinykvm, so building it produces both
## build/libkvm.a and build/tinykvm/libtinykvm.a. nlohmann/json and
## concurrentqueue are header-only INTERFACE libraries -- no archive to link.

ENGINE_SRC   = $(abs_top_srcdir)/lib/libkvm
ENGINE_BUILD = $(abs_builddir)/build
ENGINE_LIB   = $(ENGINE_BUILD)/libkvm.a

KVM_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DSHARED_KVM=ON

if BUILD_FOR_VCP
KVM_CMAKE_FLAGS += -DVARNISH_PLUS=ON
endif

all-local: $(ENGINE_LIB)

$(ENGINE_LIB):
	@test -f $(ENGINE_SRC)/ext/tinykvm/lib/CMakeLists.txt || { \
		echo "error: lib/libkvm/ext is empty -- run 'git submodule update --init --recursive'"; \
		exit 1; }
	cmake -S $(ENGINE_SRC) -B $(ENGINE_BUILD) $(KVM_CMAKE_FLAGS)
	cmake --build $(ENGINE_BUILD) --target kvm -j

clean-local:
	rm -rf $(ENGINE_BUILD)
