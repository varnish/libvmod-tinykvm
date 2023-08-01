#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

NTMP="/tmp/varnishd"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

source $file --vcp=build --optimize --build $@ --run -a :8080 -f vcl/filebench.vcl -F -n $NTMP
