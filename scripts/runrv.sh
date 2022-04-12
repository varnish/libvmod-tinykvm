#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=3M"
mkdir -p /tmp/varnishd

#source $file --vcp=build --single-process --debug --static-riscv --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS
source $file --vcp=build --optimize --static-riscv --mold --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS
#source $file --vcp=build --no-optimize --static-riscv --mold --build $@ --run -a :8080 -f vcl/riscv.vcl -F -p $BWS
