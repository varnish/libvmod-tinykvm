ACLOCAL_AMFLAGS = -I m4 -I @VARNISHAPI_DATAROOTDIR@/aclocal

DISTCHECK_CONFIGURE_FLAGS = RST2MAN=:

EXTRA_DIST = \
	get_version \
	vcp.version \
	LICENSE \
	lib/libkvm

SUBDIRS = .
SUBDIRS += src

all-local: compute-vmod
clean-local: compute-vmod-clean

compute-vmod:
	cd lib/.build && $(CMAKE) \
		-DCMAKE_C_COMPILER="$(CC)" \
		-DCMAKE_C_FLAGS="$(EXTCFLAGS) -fPIC" \
		../libkvm && $(MAKE) $(AM_MAKEFLAGS) all

compute-vmod-clean:
	cd lib/.build && $(MAKE) $(AM_MAKEFLAGS) clean
