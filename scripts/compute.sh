#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=256k"
TPS="thread_pool_stack=256k"
NTMP="/tmp/varnishd"
VMOD_API="Varnish Plus 6.0.11r5 b2e7494c5ca8813c071eb8824b7bb054f2cc9f96"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*
VCL=vcl/compute.vcl

source $file --vcp=build --optimize --mold --shared-kvm --shared-riscv --override-vmods="$VMOD_API" --build --run -a :8080 -f $VCL -F -n $NTMP -p $BWS $@
#source $file --vcp=build --no-optimize --shared-kvm --shared-riscv --mold --override-vmods="$VMOD_API" --build $@ --run -a :8080 -f $VCL -F -n $NTMP -p $BWS
#export ASAN_OPTIONS=detect_odr_violation=0
#source $file --vcp=build_debug --no-optimize --sanitize --debug=$NTMP --single-process --static-kvm --disable-mold --build --run -a :8080 -f $VCL -F -n $NTMP -p $BWS -p $TPS $@
