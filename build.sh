#!/bin/bash
export CC="ccache clang-10"
export CXX="ccache clang++-10"
VCLDIR=$PWD

mkdir -p build
pushd build
cmake ../cmake -G Ninja -DVMOD_DIR=$PWD -DVCL_DIR=$VCLDIR -DPython3_EXECUTABLE=/usr/bin/python3 -DSYSTEM_OPENSSL=ON -DUSE_LLD=ON
ninja
popd
