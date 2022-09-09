#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=256k"
TPS="thread_pool_stack=256k"
NTMP="/tmp/varnishd"
VMOD_API="Varnish Plus 6.0.10r2 e842b342bc099c0d1b5211a46f43aa3310557d0f"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

source $file --vcp=build --optimize --mold --shared-kvm --shared-riscv --override-vmods="$VMOD_API" --build --run -a :8080 -f vcl/compute.vcl -F -n $NTMP -p $BWS $@
#source $file --vcp=build --no-optimize --shared-kvm --shared-riscv --mold --override-vmods="$VMOD_API" --build $@ --run -a :8080 -f vcl/compute.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build_debug --no-optimize --sanitize --debug=$NTMP --single-process --static-kvm --disable-mold --build --run -a :8080 -f vcl/compute.vcl -F -n $NTMP -p $BWS -p $TPS $@
