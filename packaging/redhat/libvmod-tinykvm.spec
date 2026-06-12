%define debug_package %{nil}
Summary: KVM multi-tenant compute vmod for Varnish Cache.
Name: libvmod-tinykvm
Version: %{versiontag}
Release: %{releasetag}%{?dist}
License: Proprietary
Group: System Environment/Daemons
Source: %{srcname}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
%global varnishvcldir %(pkg-config varnishapi --variable=vcldir)
%global _missing_build_ids_terminate_build 0

BuildRequires: make
BuildRequires: python3
BuildRequires: varnish-plus-devel
BuildRequires: cmake
BuildRequires: gcc-c++
BuildRequires: pkgconfig(libcurl)
BuildRequires: pkgconfig(libarchive)
BuildRequires: openssl-devel

%if 0%{?rhel} >= 8
BuildRequires: python3-docutils
%else
BuildRequires: python-docutils
%endif

Requires: varnish

%description
Low-latency multi-tenant compute VMODs (kvm + tinykvm) embedding the libkvm
sandbox engine.

%prep
%setup -q -n %{srcname}

%build
# The source tarball's automake PACKAGE_TARNAME is still libvmod-compute, so it
# would install docs under %{_docdir}/libvmod-compute; point docdir at the
# package name so it matches the %files entry below.
%configure --prefix=/usr/ --docdir=%{_docdir}/%{name}
%{__make} %{?_smp_mflags} -j8

%install
[ %{buildroot} != "/" ] && %{__rm} -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot}

%clean
[ %{buildroot} != "/" ] && %{__rm} -rf %{buildroot}

%files
%{_libdir}/varnis*/vmods/libvmod_kvm.so
%{_libdir}/varnis*/vmods/libvmod_tinykvm.so
%{_mandir}/man?/*
%{_docdir}/%{name}/*
%exclude %{_libdir}/varnis*/vmods/libvmod_*.la
