#!/bin/bash
set -e
pushd $1
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD
./varnishd -a :8081 -f $PWD/../foo.vcl -F -n /tmp/foo
