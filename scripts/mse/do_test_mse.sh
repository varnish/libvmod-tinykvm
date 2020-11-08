#!/bin/bash
set -e
pushd ../build
make -j4

./mkfs.mse -f -c ../mse/small.conf
./mkfs.mse -r -c ../mse/resized.conf

echo "*** Just running a check now ***"
./mkfs.mse --check -c ../mse/small.conf
popd
