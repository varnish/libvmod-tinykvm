#!/bin/bash
PROGRAM=varnishd
PID=$(pidof $PROGRAM | awk '{ print $1 }')
set -e

g++ src/readmem.cpp -pipe -O1 -Wall -o /tmp/readmem
/tmp/readmem $PID $1
