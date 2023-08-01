#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=3M"
NTMP="/tmp/varnishd"
mkdir -p $NTMP
VCL=${1:-vcl/customer.vcl}

#source $file --vcp=build_customer --single-process --debug=/tmp/varnishd --static-riscv --build $@ --run -a :8080 -f $VCL -F -p $BWS -n $NTMP
#source $file --vcp=build_customer --no-optimize --static-riscv --mold --build $@ --run -a :8080 -f $VCL -F -p $BWS -n $NTMP
source $file --vcp=build_customer --optimize --static-riscv --mold --build $@ --run -a :8080 -f $VCL -F -p $BWS -n $NTMP
