#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=256k"
TPS="thread_pool_stack=256k"
NTMP="/tmp/varnishd"
VMOD_API="Varnish Plus 6.0.10r1 46f744d2ab5e7c30737dfcf8bbe823b01341efe8"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

source $file --vcp=build --optimize --mold --shared-kvm --shared-riscv --override-vmods="$VMOD_API" --build --run -a :8080 -f vcl/kvm_example.vcl -F -n $NTMP -p $BWS $@
#source $file --vcp=build --no-optimize --shared-kvm --shared-riscv --mold --override-vmods="$VMOD_API" --build $@ --run -a :8080 -f vcl/kvm_example.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build_debug --no-optimize --sanitize --debug=$NTMP --single-process --static-kvm --disable-mold --build --run -a :8080 -f vcl/kvm_example.vcl -F -n $NTMP -p $BWS -p $TPS $@
