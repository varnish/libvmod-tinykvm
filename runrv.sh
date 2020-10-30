#!/usr/bin/env bash
mkdir -p /tmp/varnishd
./varnishd -a :8080 -f vcl/tenancy.vcl -F
#./varnishd -a :8080 -f vcl/tenancy.vcl -C
