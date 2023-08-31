ACLOCAL_AMFLAGS = -I m4 -I @VARNISHAPI_DATAROOTDIR@/aclocal

DISTCHECK_CONFIGURE_FLAGS = RST2MAN=:

EXTRA_DIST = \
	get_version \
	vcp.version \
	LICENSE

SUBDIRS = .
SUBDIRS += src

all-local: compute-vmod
clean-local: compute-vmod-clean

compute-vmod:
	cd lib/.build && $(MAKE) $(AM_MAKEFLAGS) all

compute-vmod-clean:
	cd lib/.build && $(MAKE) $(AM_MAKEFLAGS) clean
