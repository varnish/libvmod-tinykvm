#!/bin/bash
pushd cmake
mkdir -p build
pushd build
cmake .. -G Ninja
ninja
popd
popd
