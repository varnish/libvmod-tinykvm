#!/bin/bash
mkdir -p build
pushd build
cmake ../cmake -G Ninja -DSYSTEM_OPENSSL=ON
ninja
popd
