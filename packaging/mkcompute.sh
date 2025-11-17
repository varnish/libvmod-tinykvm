set -e

# Ubuntu 24.04:
pushd varnish-lambda
mkdir -p usr/lib/varnish-plus
pushd usr/lib/varnish-plus
ln -fs ../../../../../.build .
rm -f vmods
mv .build vmods
popd
dpkg-buildpackage -nc -i
popd
mv varnish-lambda0_1.0-1_amd64.deb varnish-lambda0_1.0-1_amd64-24.04.deb
