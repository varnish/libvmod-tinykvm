# Description: Dockerfile for building libvmod-tinykvm
FROM varnish:7.6.1 AS varnish
FROM varnish AS build_vmod
ENV VMOD_BUILD_DEPS="libcurl4-openssl-dev libpcre3-dev libarchive-dev libjemalloc-dev git cmake build-essential"
USER root
WORKDIR /libvmod-tinykvm
RUN set -e; \
	export DEBIAN_FRONTEND=noninteractive; \
	apt-get update; \
	apt-get -y install /pkgs/*.deb $VMOD_DEPS $VMOD_BUILD_DEPS; \
	rm -rf /var/lib/apt/lists/*;
RUN set -e; \
	git init; \
	git remote add origin https://github.com/varnish/libvmod-tinykvm.git; \
	git fetch --depth 1; \
	git checkout FETCH_HEAD; \
	git submodule update --init --recursive;
RUN set -e; \
	mkdir -p .build; \
	cd .build; \
	cmake .. -DCMAKE_BUILD_TYPE=Release -DVARNISH_PLUS=OFF; \
	cmake --build . -j6;

FROM varnish
ENV VMOD_RUN_DEPS="libcurl4 libpcre3 libarchive13 libjemalloc2"
USER root
RUN set -e; \
	export DEBIAN_FRONTEND=noninteractive; \
	apt-get update; \
	apt-get -y install $VMOD_RUN_DEPS; \
	rm -rf /var/lib/apt/lists/*;
COPY --from=build_vmod /libvmod-tinykvm/.build/libvmod_*.so /usr/lib/varnish/vmods/
USER varnish
