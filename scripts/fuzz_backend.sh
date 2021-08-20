#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh
mkdir -p /tmp/varnishd

source $file --vcp=build --sanitize --fuzz="RESPONSE_H1" --shared-kvm --shared-riscv --build $@ --run
