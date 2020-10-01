FROM ubuntu:20.04

RUN apt update
RUN DEBIAN_FRONTEND=noninteractive apt install -y \
	build-essential git \
	cmake cmake-curses-gui \
	ninja-build \
	ccache \
	clang-10 lld-10 \
	python3 python3-pip \
	libssl-dev \
	libpcre3-dev \
	libconfig-dev \
	libbrotli-dev \
	libedit-dev
