#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

VMOD_API="Varnish Plus 6.0.10r3 4f67b6ec0d63f04560913cc7e195a3919bdf0366"
VSOURCE="$PWD/ext/varnish-cache-plus"

mkdir -p build_compute
pushd build_compute
cmake .. -G Ninja -DOVERRIDE_VMOD_API="$VMOD_API" -DVARNISH_SOURCE_DIR=$VSOURCE -DVMOD_RELEASE_BUILD=ON
ninja vmod_compute
