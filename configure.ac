AC_PREREQ([2.68])
AC_INIT([libvmod-compute],[1.0],[],[libvmod-compute])
AC_COPYRIGHT([Public Domain])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR(src/kvm/vmod_kvm.vcc)
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.12 -Wall foreign parallel-tests tar-pax subdir-objects])
AM_SILENT_RULES([yes])
AM_PROG_AR

AM_EXTRA_RECURSIVE_TARGETS([recheck])

LT_PREREQ([2.2.6])
LT_INIT([dlopen disable-static])

dnl The VMOD glue (vmod_kvm.c et al.) is C; the libkvm sandbox/runtime is C++17.
AC_PROG_CC
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

AX_PTHREAD(,[AC_MSG_ERROR([Could not configure pthreads support])])

dnl libkvm fetches programs over HTTP (libcurl) and unpacks archives
dnl (libarchive). nlohmann/json and concurrentqueue are header-only submodules.
PKG_CHECK_MODULES([CURL], [libcurl], [],
	[AC_MSG_ERROR([libcurl not found. Install libcurl4-openssl-dev.])])
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive], [],
	[AC_MSG_ERROR([libarchive not found. Install libarchive-dev.])])

dnl cmake is required to build the bundled libkvm engine (+ tinykvm) archives
AC_CHECK_PROG(CMAKE_CHECK, cmake, yes)
AS_IF([test "x$CMAKE_CHECK" != "xyes"],
	[AC_MSG_ERROR([cmake is required to build the libkvm engine.])])

AC_ARG_WITH([rst2man],
	[AS_HELP_STRING(
		[--with-rst2man=PATH],
		[Location of rst2man (auto)])],
	[RST2MAN="$withval"],
	[AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], [])])
AM_CONDITIONAL([HAVE_RST2MAN], [test -n "$RST2MAN"])

m4_ifndef([VARNISH_PREREQ], AC_MSG_ERROR([Need varnish.m4 -- see README.md]))

VARNISH_PREREQ([6.0])
VARNISH_VMODS([kvm tinykvm])

dnl Per-VMOD test lists. rotate_vcl.vtc is Varnish Plus only (see below); keep
dnl it out of the default glob and add it back under the BUILD_FOR_VCP path.
VMOD_KVM_TESTS="$(cd $srcdir/src/kvm && echo tests/*.vtc | sed 's, *tests/rotate_vcl\.vtc,,')"
VMOD_TINYKVM_TESTS="$(cd $srcdir/src/tinykvm && echo tests/*.vtc)"
AC_SUBST(VMOD_KVM_TESTS)
AC_SUBST(VMOD_TINYKVM_TESTS)

PKG_CHECK_VAR([LIBVARNISHAPI_LIBDIR], [varnishapi], [libdir])
dnl Open-source Varnish keeps its private libs in $libdir/varnish; Varnish
dnl Enterprise (Varnish Plus) uses $libdir/varnish-plus. Include both so the
dnl `make check' harness finds them regardless of which one is installed -- a
dnl non-existent entry on LD_LIBRARY_PATH is harmless.
AC_SUBST([VARNISH_LIBRARY_PATH],
	[$LIBVARNISHAPI_LIBDIR:$LIBVARNISHAPI_LIBDIR/varnish:$LIBVARNISHAPI_LIBDIR/varnish-plus])

AM_CONDITIONAL([BUILD_FOR_VCP],
	[test -d ${LIBVARNISHAPI_LIBDIR}/varnish-plus])

dnl Match the static defines the CMake build bakes into config.h (src/config.h.in)
AC_DEFINE([HAVE_OBJITERATE_F], [1], [Define if ObjIterate has the final flag])
AC_DEFINE([HAVE_GETPEEREID], [1], [Define if getpeereid() is available])
AC_DEFINE([HAVE_SETPPRIV], [1], [Define if setppriv() is available])
AC_DEFINE([HAVE_CACHE_CACHE_VARNISHD_H], [1],
	[Define if cache/cache_varnishd.h is available])

AC_CONFIG_FILES([
	Makefile
	libkvm-engine/Makefile
	src/kvm/Makefile
	src/tinykvm/Makefile
])

AC_OUTPUT

AS_ECHO(["
  ==== $PACKAGE_STRING ====

    varnishd      : $VARNISH_VERSION
    vmod install  : $vmoddir
    C++ standard  : C++17 ($CXX)
    libcurl       : $CURL_LIBS
    libarchive    : $LIBARCHIVE_LIBS
    build for VCP : $(test -d ${LIBVARNISHAPI_LIBDIR}/varnish-plus && echo yes || echo no)
"])
