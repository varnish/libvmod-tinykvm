#!/bin/bash
export CC="ccache clang-11"
export CXX="ccache clang++-11"

mkdir -p build
pushd build
cmake ../cmake -G Ninja -DSYSTEM_OPENSSL=ON -DUSE_LLD=ON
ninja
popd
