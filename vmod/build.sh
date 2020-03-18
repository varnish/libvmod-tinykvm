#!/bin/bash
SRCDIR=$(pwd)/../ext/varnish-cache-plus
set -e

mkdir -p build
pushd build
cmake .. -DVARNISH_SOURCE_DIR=$SRCDIR -DVARNISH_PLUS=ON
make -j8
popd
