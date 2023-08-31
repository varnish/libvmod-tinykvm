AC_PREREQ([2.68])
AC_INIT([libvmod-compute], [1.0])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/vmod_compute.vcc])
AC_CONFIG_HEADERS([config.h])

AC_CHECK_FUNCS([clock_gettime])

AM_INIT_AUTOMAKE([1.12 -Wall -Werror foreign parallel-tests])
AM_SILENT_RULES([yes])
AM_PROG_AR

LT_PREREQ([2.2.6])
LT_INIT([dlopen disable-static])

AC_ARG_WITH([rst2man],
	AS_HELP_STRING(
		[--with-rst2man=PATH],
		[Location of rst2man (auto)]),
	[RST2MAN="$withval"],
	[AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], [])])

VARNISH_PREREQ([6.0.0])
VARNISH_VMODS([compute])
VARNISH_COUNTERS([compute])

VTC_TESTS="$(cd $srcdir/src/ && echo tests/compute/*.vtc)"
AC_SUBST(VTC_TESTS)
AM_SUBST_NOTMAKE(VTC_TESTS)

AC_CHECK_PROGS([CMAKE], [cmake cmake3 cmake2], [no])
if test "$CMAKE" = no; then
  AC_MSG_ERROR([cmake is needed to build libkvm])
fi

abs_srcdir=$(cd "$srcdir" && pwd)

mkdir -p lib/.build ||
AC_MSG_ERROR([Could not make libkvm build directory])

(
	cd lib/.build &&
        $CMAKE \
                -DCMAKE_C_COMPILER="${CC%% *}" \
                -DCMAKE_C_FLAGS="$EXTCFLAGS -fPIC" \
                ../libkvm
)

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])

AC_OUTPUT

AS_ECHO("
	==== $PACKAGE_STRING ====

	varnish:      $VARNISH_VERSION
	prefix:       $prefix
	vmoddir:      $vmoddir
	vcldir:       $vcldir
	pkgvcldir:    $pkgvcldir

	compiler:     $CC
	cflags:       $CFLAGS
	ldflags:      $LDFLAGS
")

