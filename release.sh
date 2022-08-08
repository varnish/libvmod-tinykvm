# cmake .. -DKVM_ADNS=ON -DOVERRIDE_VMOD_API="Varnish Plus 6.0.10r1 46f744d2ab5e7c30737dfcf8bbe823b01341efe8"

pushd build_alpine
distrobox enter alpine-latest -- bash -c "\"ninja vmod_kvm\""
popd
pushd build_debian10
distrobox enter debian-10 -- bash -c "\"make -j4 vmod_kvm\""
popd
pushd build_debian11
distrobox enter debian-11 -- bash -c "\"make -j4 vmod_kvm\""
popd
pushd build_fedora
distrobox enter fedora-36 -- bash -c "\"ninja vmod_kvm\""
popd
pushd build_rlinux8
distrobox enter rockylinux-8 -- bash -c "\"make -j4 vmod_kvm\""
popd
pushd build_rlinux9
distrobox enter rockylinux-9 -- bash -c "\"make -j4 vmod_kvm\""
popd
pushd build_ubuntu22
distrobox enter ubuntu -- bash -c "\"ninja vmod_kvm\""
popd

cp build_alpine/libvmod_kvm.so ~/vcp6-kvm/vmod/alpine
cp build_debian10/libvmod_kvm.so ~/vcp6-kvm/vmod/debian10
cp build_debian11/libvmod_kvm.so ~/vcp6-kvm/vmod/debian11
cp build_fedora/libvmod_kvm.so ~/vcp6-kvm/vmod/fedora36
cp build_rlinux8/libvmod_kvm.so ~/vcp6-kvm/vmod/rockylinux8
cp build_rlinux9/libvmod_kvm.so ~/vcp6-kvm/vmod/rockylinux9
cp build/libvmod_kvm.so ~/vcp6-kvm/vmod/ubuntu20
cp build_ubuntu22/libvmod_kvm.so ~/vcp6-kvm/vmod/ubuntu22
