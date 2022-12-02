#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=3M"
NTMP="/tmp/varnishd"
mkdir -p $NTMP

#source $file --vcp=build_riscv --single-process --debug=/tmp/varnishd --static-riscv --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS -n $NTMP
source $file --vcp=build_riscv --optimize --static-riscv --disable-mold --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS -n $NTMP
#source $file --vcp=build_riscv --no-optimize --static-riscv --mold --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS -n $NTMP
