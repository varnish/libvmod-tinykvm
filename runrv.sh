#!/usr/bin/env bash
mkdir -p /tmp/varnishd
./varnishd -a :8080 -f $PWD/../vcl/tenancy.vcl -p vmod_path=$PWD -F
