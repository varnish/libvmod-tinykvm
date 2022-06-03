#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh
mkdir -p /tmp/varnishd

source $file --vcp=build_fuzz --sanitize --fuzz="HTTP" --shared-kvm --shared-riscv --build $@ --run
