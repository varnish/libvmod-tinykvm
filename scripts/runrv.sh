#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=64k"
mkdir -p /tmp/varnishd

#source $file --vcp=build --single-process --debug --static-riscv --build $@ --run -a :8080 -f vcl/tenancy.vcl -F -p $BWS
#source $file --vcp=build --optimize --static-riscv --build $@ --run -a :8080 -f vcl/tenancy.vcl -F -p $BWS
source $file --vcp=build --no-optimize --static-riscv --build $@ --run -a :8080 -f vcl/tenancy.vcl -F -p $BWS
