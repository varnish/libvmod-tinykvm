#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=64k"
NTMP="/tmp/varnishd"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

source $file --vcp=build --optimize --static-kvm --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build --no-optimize --static-kvm --mold --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
#source $file --vcp=build --no-optimize --debug --single-process --static-kvm --mold --build $@ --run -a :8080 -f vcl/kvm.vcl -F -n $NTMP -p $BWS
