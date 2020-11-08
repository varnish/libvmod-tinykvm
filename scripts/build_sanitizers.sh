#!/bin/bash
export CC="ccache clang-11"
export CXX="ccache clang++-11"
export LSAN_OPTIONS=suppressions=$PWD/ext/varnish-cache-plus/tools/lsan.suppr

mkdir -p build_san
pushd build_san
cmake ../cmake -G Ninja -DSYSTEM_OPENSSL=ON -DUSE_LLD=ON -DSANITIZE=ON
ninja
popd
