#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh
mkdir -p /tmp/varnishd

source $file --vc=fuzz_vc --sanitize --fuzz="HTTP" --build $@ --run
