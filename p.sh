#!/bin/bash
BINARY=./build/varnishd
VARNISH="$BINARY -a :8080 -b :8081 -F"

#trap 'echo "enter 0 to exit"' SIGINT

pushd build
cmake .. -DAUTOFDO=OFF
make -j8
popd

$VARNISH
