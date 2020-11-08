#!/bin/bash

./build/varnishd -a :6081 -a localhost:8443,proxy -T localhost:6082 -f /home/alf/git/varnish_autoperf/vcl/mse_bench.vcl -s mse,/var/lib/mse -p vcc_allow_inline_c=on -n /tmp/varnishd
