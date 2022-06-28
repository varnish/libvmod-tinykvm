#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=256k"
TPS="thread_pool_stack=256k"
NTMP="/tmp/varnishd"
VMOD_API="Varnish Plus 6.0.9r7 960d44c42603b8893b55a16fe713ed573dbb4c4c"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

#source $file --vcp=build --optimize --static-kvm --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
source $file --vcp=build --no-optimize --shared-kvm --shared-riscv --mold --override-vmods="$VMOD_API" --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build_debug --no-optimize --sanitize --debug=$NTMP --single-process --static-kvm --disable-mold --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS -p $TPS
