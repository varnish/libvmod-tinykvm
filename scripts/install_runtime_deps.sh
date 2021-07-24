#!/usr/bin/env bash
set -e
sudo apt update
sudo apt install -y \
	cmake \
	clang-10 lld-10 \
	libbrotli1 \
	libconfig9 \
	libnuma1
