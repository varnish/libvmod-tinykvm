#!/bin/bash
set -e
#export CC=clang-9
#export CXX=clang++-9
export CC="ccache $HOME/llvm/install/bin/clang-11"
export CXX="ccache $HOME/llvm/install/bin/clang++-11"
FOLDER=build_vmodfuzz

mkdir -p $FOLDER
pushd $FOLDER
cmake ../cmake -G Ninja -DSINGLE_PROCESS=ON -DVARNISH_PLUS=ON -DBUILD_VMODS=ON -DLIBFUZZER=ON -DFUZZER=VMOD -DSANITIZE=ON -DLTO_ENABLE=ON
ninja
popd

export ASAN_OPTIONS=disable_coredump=0::unmap_shadow_on_exit=1
export ASAN_SYMBOLIZER_PATH=$HOME/llvm/install/bin/llvm-symbolizer

./$FOLDER/varnishd
