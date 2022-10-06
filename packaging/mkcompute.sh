
pushd varnish-compute
dpkg-buildpackage -nc -i
popd
sudo dpkg -i varnish-compute0_1.0-1_amd64.deb
