#!/usr/bin/env bash
set -e
sudo apt update
sudo apt install -y \
	cmake \
	clang-12 lld-12 \
	libstdc++6 \
	libbrotli1 \
	libconfig9 \
	libnuma1
# Varnish user needs KVM access
sudo addgroup $USER kvm
