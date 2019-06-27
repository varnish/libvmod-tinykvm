#!/bin/bash
PROGRAM=varnishd
PID=$(pidof $PROGRAM | awk '{ print $1 }')
FOLDED=./FlameGraph/out.folded
set -e

function process {
	#tail --pid=$PID -f /dev/null
	sleep 1
	sudo chown gonzo perf.data
	perf script | ./FlameGraph/stackcollapse-perf.pl > $FOLDED
	./FlameGraph/flamegraph.pl $FOLDED > perf-$PROGRAM.svg
}
trap process SIGINT

echo "Using PID $PID"
sudo perf record -F 99 -a -g -p $PID --all-user
