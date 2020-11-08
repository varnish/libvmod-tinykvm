#!/bin/bash
PROGRAM=varnishd
PID=$(pidof $PROGRAM | awk '{ print $1 }')
set -e

function process {
	exit 0
}
trap process SIGINT

echo "Using PID $PID"
sudo perf c2c record -g -p $PID --all-user
