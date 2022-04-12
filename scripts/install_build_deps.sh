#!/usr/bin/env bash
set -e
sudo apt update
DEBIAN_FRONTEND=noninteractive sudo -E apt install -y \
	build-essential git \
	cmake \
	ninja-build \
	ccache \
	clang-12 lld-12 \
	python3 python3-pip \
	libcurl4-openssl-dev \
	libssl-dev \
	libpcre3-dev \
	libconfig-dev \
	libbrotli-dev \
	libedit-dev \
	libncursesw5-dev \
	numactl \
	libnuma-dev
