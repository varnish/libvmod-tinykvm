#!/bin/bash
PROF=perf.afdo
PID=$(pidof varnishd)
set -e

OCPERF=$(which ocperf.py)
#sudo perf record -e cpu/event=0xc4,umask=0x20,name=br_inst_retired_near_taken,period=400009/pp -b -o perf.data -p $PID
sudo $OCPERF record -b -e br_inst_retired.near_taken:pp -o perf.data -p $PID
#sudo $OCPERF record -b -g -e br_inst_retired.near_taken -o perf.data -p $PID

sleep 1
sudo chown gonzo perf.data

/usr/local/bin/create_gcov --profiler=perf --profile perf.data --binary build/varnishd --gcov_version 1 --gcov $PROF

exit 0
pushd build
cmake .. -DAUTOFDO=ON -DPROFILE=../$PROF
make -j8
popd
