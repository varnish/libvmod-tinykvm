#!/bin/bash
export CC="ccache clang-10"
export CXX="ccache clang++-10"

mkdir -p build_vc
pushd build_vc
cmake .. -G Ninja -DVARNISH_PLUS=OFF -DUSE_LLD=ON
ninja
popd
