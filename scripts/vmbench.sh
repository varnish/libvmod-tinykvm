#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

BWS="workspace_backend=1500k"
mkdir -p /tmp/varnishd

source $file --vcp=build --optimize --static-sandbox --build $@ --run -a :8080 -f vcl/tenancy.vcl -F -p $BWS -p vsl_flush=off
