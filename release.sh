# cmake .. -DKVM_ADNS=ON -DOVERRIDE_VMOD_API="Varnish Plus 6.0.10r1 46f744d2ab5e7c30737dfcf8bbe823b01341efe8"

cp build_alpine/libvmod_kvm.so ~/vcp6-kvm/vmod/alpine
cp build_debian10/libvmod_kvm.so ~/vcp6-kvm/vmod/debian10
cp build_debian11/libvmod_kvm.so ~/vcp6-kvm/vmod/debian11
cp build_fedora/libvmod_kvm.so ~/vcp6-kvm/vmod/fedora36
cp build_rlinux8/libvmod_kvm.so ~/vcp6-kvm/vmod/rockylinux8
cp build_rlinux9/libvmod_kvm.so ~/vcp6-kvm/vmod/rockylinux9
cp build/libvmod_kvm.so ~/vcp6-kvm/vmod/ubuntu20
cp build_ubuntu22/libvmod_kvm.so ~/vcp6-kvm/vmod/ubuntu22
