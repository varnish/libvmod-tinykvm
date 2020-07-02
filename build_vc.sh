#!/bin/bash
export CC="ccache clang-11"
export CXX="ccache clang++-11"

mkdir -p build_vc
pushd build_vc
cmake ../cmake -G Ninja -DVARNISH_PLUS=OFF -DUSE_LLD=ON
ninja
popd
