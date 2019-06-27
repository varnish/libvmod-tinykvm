#! /bin/bash
set -e

echo -e "\n>>> Installing dependencies"
sudo pip install setuptools wheel
pip -q install gprof2dot
sudo apt install -y graphviz

BINARY=build/varnishd
echo -e "\n>>> Generating callgraph.svg from $BINARY"
gprof $BINARY | gprof2dot | dot -Tsvg -o callgraph.svg
firefox callgraph.svg
