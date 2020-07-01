#!/bin/bash
export CC="ccache clang-11"
export CXX="ccache clang++-11"

mkdir -p build_vmod
pushd build_vmod
cmake ../cmake -G Ninja -DBUILD_VMODS=ON -DSYSTEM_OPENSSL=ON
ninja
popd
