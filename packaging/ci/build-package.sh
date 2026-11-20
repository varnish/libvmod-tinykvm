#!/usr/bin/env bash
#
# Build a native libvmod-tinykvm package (.deb or .rpm) for Varnish Enterprise,
# matching the enterprise package naming used by e.g. libvmod-sledge:
#
#   libvmod-tinykvm_1.0~6.0.18r2-1~noble_amd64.deb
#   libvmod-tinykvm-1.0~6.0.18r2-1.el9.x86_64.rpm
#
# The version embeds the *installed* Varnish Enterprise version (varnish-plus),
# so the package always tracks the ABI it was linked against. The 60-enterprise
# packagecloud repo is publicly installable -- no token required.
#
# This script is meant to run *inside* a target-distro container (see
# .github/workflows/build-packages.yml). It installs its own build dependencies.
#
# Inputs (environment):
#   PKG_FAMILY   deb | rpm                         (required)
#   PKG_CODENAME deb distro codename, e.g. noble   (required for deb)
#   PKG_DIST     rpm dist tag, e.g. el9 / amzn2023 (required for rpm; informational)
#   VCP_VERSION  Varnish Enterprise upstream version to pin, e.g. 6.0.18r2.
#                Use "latest" or leave unset to install whatever is newest.
#   PKG_REVISION package revision, e.g. 1 for a release (the "-1" above).
#                Leave unset for a snapshot build, which uses a timestamp,
#                as the other enterprise VMODs do.
#   OUTDIR       where built packages are copied   (default: ./packages)
#   ENTERPRISE_REPO   packagecloud repo to install varnish-plus from
#                     (default: varnishplus/60-enterprise, the public prod repo;
#                      e.g. varnishplus/60-enterprise-staging for staging)
#   PACKAGECLOUD_TOKEN  master/read token for a private repo (e.g. staging).
#                       Leave unset for the public production repo.
#
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$REPO_ROOT"

PKG_FAMILY=${PKG_FAMILY:?PKG_FAMILY must be set to 'deb' or 'rpm'}
OUTDIR=${OUTDIR:-"$REPO_ROOT/packages"}
VCP_VERSION=${VCP_VERSION:-latest}
PKG_REVISION=${PKG_REVISION:-$(date -u +%Y%m%d%H%M%S)}
ENTERPRISE_REPO=${ENTERPRISE_REPO:-"varnishplus/60-enterprise"}
PACKAGECLOUD_TOKEN=${PACKAGECLOUD_TOKEN:-}

log() { echo "==> $*"; }

# packagecloud install-script URL for the configured enterprise repo. A private
# repo (e.g. staging) needs a master/read token, supplied as the HTTP basic-auth
# username with an empty password; the install script then writes a per-repo
# read token into the generated apt/yum config. The public prod repo needs none.
pkgcloud_install_url() {
	local script=$1   # script.deb.sh | script.rpm.sh
	if [ -n "$PACKAGECLOUD_TOKEN" ]; then
		echo "https://${PACKAGECLOUD_TOKEN}:@packagecloud.io/install/repositories/${ENTERPRISE_REPO}/${script}"
	else
		echo "https://packagecloud.io/install/repositories/${ENTERPRISE_REPO}/${script}"
	fi
}

# libvmod-compute's own version, taken straight from configure.ac so we don't
# need a generated ./configure to exist yet.
compute_version() {
	sed -n 's/^AC_INIT(\[[^]]*\],\[\([^]]*\)\].*/\1/p' configure.ac
}

# ---------------------------------------------------------------------------
# Debian / Ubuntu
# ---------------------------------------------------------------------------
build_deb() {
	: "${PKG_CODENAME:?PKG_CODENAME must be set for deb builds}"
	export DEBIAN_FRONTEND=noninteractive

	log "Installing Varnish Enterprise repo + build dependencies ($PKG_CODENAME)"
	apt-get update
	apt-get install -y --no-install-recommends \
		apt-transport-https ca-certificates curl gnupg
	curl -fsSL "$(pkgcloud_install_url script.deb.sh)" | bash
	apt-get update

	# Resolve the varnish-plus package spec. "latest" leaves it unversioned;
	# a pinned upstream version (e.g. 6.0.18r2) is mapped to the full distro
	# version (e.g. 6.0.18r2-1~noble) by querying the repo, so we don't have to
	# guess the packaging release or codename suffix.
	local vcp_pkg=varnish-plus vcp_dev=varnish-plus-dev
	if [ "$VCP_VERSION" != latest ]; then
		local full
		full=$(apt-cache madison varnish-plus \
			| awk -F'|' '{v=$2; gsub(/ /,"",v); print v}' \
			| grep -E "^${VCP_VERSION}-" | sort -Vr | head -1)
		[ -n "$full" ] || { echo "ERROR: varnish-plus ${VCP_VERSION} not available for ${PKG_CODENAME}" >&2; exit 1; }
		vcp_pkg="varnish-plus=$full"; vcp_dev="varnish-plus-dev=$full"
		log "Pinning Varnish Enterprise to $full"
	fi

	apt-get install -y --no-install-recommends \
		build-essential debhelper dh-autoreconf devscripts fakeroot \
		autoconf automake libtool pkg-config cmake make \
		libcurl4-openssl-dev libssl-dev libarchive-dev python3 python3-docutils \
		"$vcp_pkg" "$vcp_dev"

	# Ensure a C++20-capable compiler (the bundled tinykvm engine requires
	# C++20). Ubuntu jammy defaults to g++-11; pull in g++-12 and make it the
	# default. Newer distros already ship a fit compiler.
	if [ "$PKG_CODENAME" = "jammy" ]; then
		apt-get install -y --no-install-recommends g++-12 gcc-12
		update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100
		update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100
		export CC=gcc-12 CXX=g++-12
	fi

	# Derive the enterprise version from the installed package:
	#   6.0.18r2-1~noble  ->  upstream 6.0.18r2
	local vcp_full vcp_nodist vcp_up cv srcver
	vcp_full=$(dpkg-query -W -f='${Version}' varnish-plus)
	vcp_nodist=${vcp_full%%~*}      # strip ~<codename>
	vcp_up=${vcp_nodist%-*}         # upstream version
	cv=$(compute_version)
	srcver="${cv}~${vcp_up}"
	log "Versions: compute=$cv varnish-plus=$vcp_full -> ${srcver}-${PKG_REVISION}~${PKG_CODENAME}"

	# Bootstrap the autotools build ourselves. We deliberately do not rely on
	# dh-autoreconf: Makefile.am's ACLOCAL_AMFLAGS references a Make-substituted
	# var (${VARNISHAPI_DATAROOTDIR}) that bare `autoreconf' cannot expand, but
	# ./autogen.sh passes aclocal the correct -I paths.
	log "Bootstrapping autotools (./autogen.sh)"
	./autogen.sh

	log "Preparing debian/ tree"
	rm -rf debian
	cp -r packaging/debian debian
	sed -e "s/@SRCVER@/${srcver}/g" \
	    -e "s/@PKGVER@/${PKG_REVISION}/g" \
	    -e "s/@DISTVER@/${PKG_CODENAME}/g" \
	    -e "s/UNRELEASED/${PKG_CODENAME}/g" \
	    packaging/debian/changelog > debian/changelog
	# A modern source format keeps dpkg-buildpackage from complaining; binary
	# (-b) builds never produce a source package so no orig tarball is needed.
	mkdir -p debian/source
	echo '3.0 (native)' > debian/source/format

	log "Building .deb"
	dpkg-buildpackage -us -uc -b

	mkdir -p "$OUTDIR"
	cp ../libvmod-tinykvm_*.deb "$OUTDIR"/
	log "Built:"; ls -1 "$OUTDIR"/*.deb
}

# ---------------------------------------------------------------------------
# RHEL family (AlmaLinux / Amazon Linux)
# ---------------------------------------------------------------------------
build_rpm() {
	local PM=dnf
	command -v dnf >/dev/null 2>&1 || PM=yum

	log "Installing Varnish Enterprise repo + build dependencies (${PKG_DIST:-rpm})"
	# curl is preinstalled as curl-minimal on AlmaLinux/Amazon and conflicts with
	# the full curl package, so only pull it in if the binary is actually absent.
	command -v curl >/dev/null 2>&1 || $PM install -y --allowerasing curl
	$PM install -y ca-certificates findutils which 2>/dev/null || true
	curl -fsSL "$(pkgcloud_install_url script.rpm.sh)" | bash

	# EL8 hides modular-named packages (varnish-plus) behind DNF modular
	# filtering unless the third-party repo opts out. Harmless elsewhere.
	sed -i '/^\[/a module_hotfixes=true' /etc/yum.repos.d/*60-enterprise*.repo 2>/dev/null || true

	# varnish-plus pulls libisal/libunwind, which on Enterprise Linux live in
	# EPEL + CRB/PowerTools. Amazon Linux 2023 ships them in its own repos.
	case "${PKG_DIST:-}" in
		el8)
			$PM install -y epel-release dnf-plugins-core
			$PM config-manager --set-enabled powertools \
				|| $PM config-manager --set-enabled PowerTools || true
			;;
		el9|el10)
			$PM install -y epel-release dnf-plugins-core
			$PM config-manager --set-enabled crb || true
			;;
	esac

	# Resolve the varnish-plus package spec. "latest" leaves it unversioned; a
	# pinned upstream version (e.g. 6.0.18r2) becomes a NEVRA glob that matches
	# any packaging release / dist tag (e.g. varnish-plus-6.0.18r2-1.el9).
	local vcp_pkg=varnish-plus vcp_dev=varnish-plus-devel
	if [ "$VCP_VERSION" != latest ]; then
		vcp_pkg="varnish-plus-${VCP_VERSION}-*"
		vcp_dev="varnish-plus-devel-${VCP_VERSION}-*"
		log "Pinning Varnish Enterprise to ${VCP_VERSION}"
	fi

	# tar/gzip are needed by `make dist' and rpmbuild and are not preinstalled on
	# the Amazon Linux 2023 base image.
	$PM install -y \
		rpm-build make cmake python3 pkgconfig libcurl-devel openssl-devel libarchive-devel \
		tar gzip autoconf automake libtool \
		"$vcp_pkg" "$vcp_dev"

	# C++20 compiler (the bundled tinykvm engine requires C++20). el9/el10
	# (gcc 11/14) and amzn2023 (gcc 11) already do C++20 with the base compiler.
	# el8's gcc 8 is too old, so use gcc-toolset-13 there (base gcc-c++ is still
	# installed to satisfy the spec's BuildRequires).
	case "${PKG_DIST:-}" in
		el8)
			$PM install -y gcc-c++ gcc-toolset-13-gcc-c++ gcc-toolset-13-annobin-plugin-gcc
			# shellcheck disable=SC1091
			source /opt/rh/gcc-toolset-13/enable
			;;
		*)
			$PM install -y gcc-c++
			;;
	esac

	ensure_rst2man

	# Derive the enterprise version from the installed package.
	local vcp_up dist cv srcver
	vcp_up=$(rpm -q --qf '%{VERSION}' varnish-plus)
	dist=$(rpm --eval '%{?dist}')
	cv=$(compute_version)
	srcver="${cv}~${vcp_up}"
	log "Versions: compute=$cv varnish-plus=$(rpm -q --qf '%{VERSION}-%{RELEASE}' varnish-plus) -> ${srcver}-${PKG_REVISION}${dist}"

	# rpmbuild needs a source tarball that already contains ./configure, so
	# bootstrap and use the automake `make dist' tarball (top dir libvmod-compute-<cv>).
	log "Generating source tarball via make dist"
	./autogen.sh
	./configure
	make dist
	local srcname="libvmod-compute-${cv}"
	local rpmtop="$HOME/rpmbuild"
	mkdir -p "$rpmtop/SOURCES"
	cp "${srcname}.tar.gz" "$rpmtop/SOURCES/${srcname}.tgz"

	log "Building .rpm"
	rpmbuild -bb \
		--define "_topdir $rpmtop" \
		--define "versiontag ${srcver}" \
		--define "releasetag ${PKG_REVISION}" \
		--define "srcname ${srcname}" \
		packaging/redhat/libvmod-tinykvm.spec

	mkdir -p "$OUTDIR"
	find "$rpmtop/RPMS" -name '*.rpm' -exec cp {} "$OUTDIR"/ \;
	log "Built:"; ls -1 "$OUTDIR"/*.rpm
}

# rst2man (from docutils) is required to build the man pages. It is not always
# packaged on EL/Amazon, so fall back to pip.
ensure_rst2man() {
	local PM=dnf
	command -v dnf >/dev/null 2>&1 || PM=yum
	$PM install -y python3-docutils 2>/dev/null || true
	if command -v rst2man >/dev/null 2>&1 || command -v rst2man.py >/dev/null 2>&1; then
		return
	fi
	log "rst2man not found from packages; installing docutils via pip"
	$PM install -y python3-pip
	pip3 install --quiet docutils || pip3 install --quiet --break-system-packages docutils
}

case "$PKG_FAMILY" in
	deb) build_deb ;;
	rpm) build_rpm ;;
	*) echo "Unknown PKG_FAMILY: $PKG_FAMILY (expected deb or rpm)" >&2; exit 1 ;;
esac
