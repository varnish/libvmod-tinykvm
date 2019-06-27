#!/bin/bash
PROF=perf.afdo
PID=$1
set -e

#sudo perf record -e BR_INST_EXEC:TAKEN -b -c 1 -o perf.data -- $VARNISH
export OCPERF=`whereis ocperf.py | awk -v N=$N '{print $2}'`
sudo $OCPERF record -e br_inst_retired.near_taken:pp -b -o perf.data -p $PID
sudo chown gonzo perf.data

/usr/local/bin/create_gcov --profiler=perf --profile perf.data.old --binary build/varnishd --gcov $PROF --gcov_version 1

pushd build
cmake .. -DAUTOFDO=ON -DPROFILE=../$PROF
make -j8
popd
