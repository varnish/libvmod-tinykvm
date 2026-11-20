#!/usr/bin/env bash

set -e

export RUBYOPT=-W0

user="varnishplus"

function upload {
    distro=$1
    shift
    distrel=$1
    shift
    comp=$1
    shift
    set -x
    package_cloud push $user/${comp}-staging/$distro/$distrel $@
    set +x
}

for i in jammy noble resolute; do
    echo "***** Ubuntu - $i *****"
    upload ubuntu $i 60-enterprise packages/ubuntu-$i/*.deb
done
for i in bookworm trixie; do
    echo "***** Debian - $i *****"
    upload debian $i 60-enterprise packages/debian-$i/*.deb
done
for i in 8 9 10; do
    echo "***** Almalinux - $i *****"
    upload el $i 60-enterprise packages/almalinux-$i/*.rpm
done

for i in 2023; do
    echo "***** Amazonlinux - $i *****"
    upload amazon $i 60-enterprise packages/amazonlinux-$i/*.rpm
done
