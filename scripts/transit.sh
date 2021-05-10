#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

export CFLAGS="-O2 -march=native"
BWS="workspace_backend=1500k"
mkdir -p /tmp/varnishd

source $file --vcp=build --no-optimize --shared-sandbox --build $@ --run -a :8080 -f vcl/transit.vcl -F -p $BWS
