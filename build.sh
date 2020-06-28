#!/bin/bash
mkdir -p build
pushd build
cmake ../cmake -G Ninja
ninja
popd
