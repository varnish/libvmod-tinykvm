#!/bin/bash
mkdir -p build
pushd build
cmake ../cmake -G Ninja -DUSE_LLD=ON
ninja
popd
