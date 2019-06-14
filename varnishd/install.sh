#!/bin/bash
set -e
cp ../bin/varnishd/.libs/varnishd $1/
cp ../lib/libvarnish_sslhelper/.libs/libvarnish-sslhelper.so.0.0.0 $1/
cp ../lib/libmse/.libs/libmse.so $1/
pushd $1
ln -s libvarnish-sslhelper.so.0.0.0 libvarnish-sslhelper.so.0
popd
