#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=256k"
TPS="thread_pool_stack=256k"
NTMP="/tmp/varnishd"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

#source $file --vcp=build --optimize --static-kvm --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
source $file --vcp=build --no-optimize --static-kvm --shared-riscv --mold --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build_debug --no-optimize --sanitize --debug=$NTMP --single-process --static-kvm --disable-mold --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS -p $TPS
