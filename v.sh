#!/bin/bash
BINARY=./build/varnishd

pushd build
cmake .. -DAUTOFDO=OFF
make -j8
popd

$BINARY -a :8080 -b :8081 -F
