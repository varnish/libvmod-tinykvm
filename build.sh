#!/bin/bash
set -e

mkdir -p .build
pushd .build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVARNISH_PLUS=OFF -G Ninja
ninja
popd

echo "Installing vmod into /usr/lib/varnish/vmods/"
sudo cp .build/libvmod_*.so /usr/lib/varnish/vmods/
