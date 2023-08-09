#!/usr/bin/env bash
set -e
# RISC-V sandbox
pushd vmods/riscv
git submodule update --init --depth 1 libriscv
pushd libriscv
#NO_BUILD=1 ./setup.sh
popd
popd
# KVM sandbox
pushd vmods/kvm
git submodule update --init --depth 1 tinykvm
popd
# Varnish
pushd ext
git submodule update --init --depth 1 openssl-cmake
git submodule update --init --depth 1 varnish-cache-plus
popd
pushd cmake/riscv_sandbox/ext
git submodule update --init --depth 1 json
popd
