#!/bin/bash
export CC="ccache clang-11"
export CXX="ccache clang++-11"

mkdir -p build_work
pushd build_work
cmake ../cmake -G Ninja -DVARNISH_BRANCH_DIR="work-plus" -DSYSTEM_OPENSSL=ON -DUSE_LLD=ON
ninja
popd
