#!/usr/bin/env bash
dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file=$dir/cmaketool.sh

mkdir -p /tmp/varnishd
source $file --vcp=build --optimize --static-sandbox --build --run -a :8080 -f vcl/tenancy.vcl -F
