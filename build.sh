#!/bin/bash
pushd cmake
mkdir -p build
pushd build
cmake ..
make -j8
popd
popd
