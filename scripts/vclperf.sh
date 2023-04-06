#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=1M"
NTMP="/tmp/varnishd"
mkdir -p $NTMP

source $file --vcp=build_vcl --single-process --optimize --shared-riscv --build $@ --run -a :8080 -f vcl/vclperf.vcl -F -p $BWS -n $NTMP
