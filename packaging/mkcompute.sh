set -e

# Ubuntu 22.04:
pushd varnish-compute
mkdir -p usr/lib/varnish-plus
pushd usr/lib/varnish-plus
ln -fs ../../../../../build_ubuntu22 .
rm -f vmods
mv build_ubuntu22 vmods
popd
dpkg-buildpackage -nc -i
popd
mv varnish-compute0_1.0-1_amd64.deb varnish-compute0_1.0-1_amd64-22.04.deb

# Ubuntu 20.04:
pushd varnish-compute
pushd usr/lib/varnish-plus
ln -fs ../../../../../build_ubuntu20 .
rm -f vmods
mv build_ubuntu20 vmods
popd
dpkg-buildpackage -nc -i
popd
mv varnish-compute0_1.0-1_amd64.deb varnish-compute0_1.0-1_amd64-20.04.deb
