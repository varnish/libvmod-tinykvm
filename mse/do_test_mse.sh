#!/bin/bash
set -e
pushd ../build
make -j4

./mkfs.mse -f -c ../mse/small.conf
#gdb --args ./mkfs.mse -r -c ../mse/resized.conf
./mkfs.mse -r -c ../mse/resized.conf
popd
