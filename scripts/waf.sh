#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=128k"
NTMP="/tmp/varnishd"
mkdir -p $NTMP
# fixes VMOD cache bugs
rm -rf /tmp/varnishd/*

#source $file --vcp=build --debug --single-process --shared-kvm --build $@ --run -a :8080 -f vcl/waf.vcl -F -n $NTMP -p $BWS
source $file --vcp=build --debug --shared-kvm --build $@ --run -a :8080 -f vcl/waf.vcl -F -n $NTMP -p $BWS
