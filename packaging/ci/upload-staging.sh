#!/usr/bin/env bash
#
# Push freshly built libvmod-tinykvm packages to the Varnish Enterprise *staging*
# packagecloud repo. Called by the "upload" job of the Packages workflow when
# it runs in staging mode; prod builds never call this.
#
# Packages are expected under $ARTIFACTS_DIR in the per-artifact layout produced
# by actions/download-artifact (no merge-multiple), i.e. one subdirectory per
# distro/arch named after its build artifact:
#
#   artifacts/libvmod-tinykvm-ubuntu-noble-amd64/libvmod-tinykvm_...~noble_amd64.deb
#   artifacts/libvmod-tinykvm-almalinux-9-arm64/libvmod-tinykvm-...el9.aarch64.rpm
#
# The distro/version push target is derived from the subdirectory name, so the
# .deb/.rpm filenames themselves are never parsed. packagecloud infers the
# architecture from each package file, so amd64 and arm64 push to the same
# distro/version target.
#
# Inputs (environment):
#   PACKAGECLOUD_TOKEN  packagecloud API token with *push* access to STAGING_REPO
#                       (required).
#   STAGING_REPO        packagecloud repo to push to, "<user>/<repo>"
#                       (default: varnishplus/60-enterprise-staging).
#   ARTIFACTS_DIR       directory holding the per-artifact subdirectories
#                       (default: ./artifacts).
set -euo pipefail

: "${PACKAGECLOUD_TOKEN:?PACKAGECLOUD_TOKEN must be set (packagecloud push token)}"
STAGING_REPO=${STAGING_REPO:-varnishplus/60-enterprise-staging}
ARTIFACTS_DIR=${ARTIFACTS_DIR:-artifacts}

export PACKAGECLOUD_TOKEN
# package_cloud is a chatty Ruby gem; silence deprecation warnings.
export RUBYOPT=-W0

log() { echo "==> $*"; }

if ! command -v package_cloud >/dev/null 2>&1; then
	log "Installing the package_cloud gem"
	gem install --no-document package_cloud \
		|| sudo gem install --no-document package_cloud
fi

# Map an artifact directory name (libvmod-tinykvm-<label>-<arch>) to the
# packagecloud "<distro>/<version>" push target. The order is irrelevant since
# the labels are distinct substrings.
pc_target() {
	case "$1" in
		*ubuntu-jammy*)     echo ubuntu/jammy ;;
		*ubuntu-noble*)     echo ubuntu/noble ;;
		*ubuntu-resolute*)  echo ubuntu/resolute ;;
		*debian-bookworm*)  echo debian/bookworm ;;
		*debian-trixie*)    echo debian/trixie ;;
		*almalinux-8*)      echo el/8 ;;
		*almalinux-9*)      echo el/9 ;;
		*almalinux-10*)     echo el/10 ;;
		*amazonlinux-2023*) echo amazon/2023 ;;
		*) return 1 ;;
	esac
}

[ -d "$ARTIFACTS_DIR" ] || { echo "ERROR: artifacts dir '$ARTIFACTS_DIR' not found" >&2; exit 1; }

shopt -s nullglob
pushed=0
for dir in "$ARTIFACTS_DIR"/*/; do
	name=$(basename "$dir")
	if ! target=$(pc_target "$name"); then
		echo "WARNING: no packagecloud mapping for '$name', skipping" >&2
		continue
	fi
	for pkg in "$dir"*.deb "$dir"*.rpm; do
		log "Pushing $(basename "$pkg") -> $STAGING_REPO/$target"
		package_cloud push "$STAGING_REPO/$target" "$pkg"
		pushed=$((pushed + 1))
	done
done

[ "$pushed" -gt 0 ] || { echo "ERROR: no packages found under '$ARTIFACTS_DIR'" >&2; exit 1; }
log "Uploaded $pushed package(s) to $STAGING_REPO."
