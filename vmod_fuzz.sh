#!/bin/bash
set -e
export CC=$HOME/llvm/install/bin/clang-10
export CXX=$HOME/llvm/install/bin/clang++-10

mkdir -p build
pushd build
cmake ../cmake -DSINGLE_PROCESS=ON -DVARNISH_PLUS=ON -DLIBFUZZER=ON -DFUZZER=VMOD -DSANITIZE=ON
make -j16
popd

export ASAN_OPTIONS=disable_coredump=0::unmap_shadow_on_exit=1
export ASAN_SYMBOLIZER_PATH=$HOME/llvm/install/bin/llvm-symbolizer

./build/varnishd
