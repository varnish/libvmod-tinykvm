AM_CFLAGS = $(VARNISHAPI_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/lib/libkvm

# Modules

vmod_LTLIBRARIES = \
	libvmod_compute.la

libvmod_compute_la_LDFLAGS = $(VMOD_LDFLAGS)  \
	-L$(abs_top_srcdir)/lib/.build \
	-l:libkvm.a \
	-l:tinykvm/libtinykvm.a \
	-lstdc++  \
	-larchive \
	-lcurl \
	-L$(VARNISHAPI_VMODDIR)/../ \
	-ladns \
	-Wl,-rpath,$(VARNISHAPI_VMODDIR)/../

libvmod_compute_la_SOURCES = \
	vmod_compute.c \
	vmod_compute.h \
	kvm_backend.c \
	kvm_backend.h \
	kvm_live_update.c \
	kvm_live_update.h \
	kvm_post.c \
	kvm_settings.h \
	to_string.c \
	vmod_compute_event.c

nodist_libvmod_compute_la_SOURCES = \
	vcc_compute_if.c \
	vcc_compute_if.h

EXTRA_DIST = \
	kvm_streaming_backend.c \
	vmod_kvm.h

kvm_backend.c \
	kvm_backend.h \
	kvm_live_update.c \
	kvm_live_update.h \
	kvm_post.c \
	kvm_settings.h \
	to_string.c \
	vmod_compute_event.c: vcc_compute_if.h

@BUILD_VMOD_COMPUTE@
@BUILD_VSC_COMPUTE@

# Test suite

AM_TESTS_ENVIRONMENT = \
	PATH="$(abs_builddir):$(VARNISH_TEST_PATH):$(PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = varnishtest -v
AM_VTC_LOG_FLAGS = \
	-p vcl_path="$(abs_top_srcdir)/vcl:$(VARNISHAPI_VCLDIR)" \
	-p vmod_path="$(abs_builddir)/.libs:$(vmoddir):$(VARNISHAPI_VMODDIR)"

TESTS = @VTC_TESTS@

# Documentation

dist_doc_DATA = \
	vmod_compute.vcc \
	$(TESTS)

dist_man_MANS = \
	vmod_compute.3


.rst.1:
	$(AM_V_GEN) $(RST2MAN) $< $@
