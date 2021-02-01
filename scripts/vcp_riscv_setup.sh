#!/usr/bin/env bash
set -e
pushd vmods/riscv
git submodule update --init --depth 1 libriscv
pushd libriscv
NO_BUILD=1 ./setup.sh
popd
popd
pushd ext
git submodule update --init --depth 1 openssl-cmake
git submodule update --init --depth 1 varnish-cache-plus
popd
pushd cmake/sandbox/ext
git submodule update --init --depth 1 json
popd
