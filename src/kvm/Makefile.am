## libvmod_kvm -- the KVM multi-tenancy VMOD.
##
## The VMOD glue is C; the libkvm sandbox/runtime is C++17, built as static
## archives by ../../libkvm-engine and linked in here. This Makefile.am lives
## alongside the (untouched) CMakeLists.txt so the CMake developer build keeps
## working; automake and cmake ignore each other.

KVM_ENGINE_LIB    = $(top_builddir)/libkvm-engine/build/libkvm.a
TINYKVM_ENGINE_LIB = $(top_builddir)/libkvm-engine/build/tinykvm/libtinykvm.a

AM_CPPFLAGS = \
	-I$(srcdir) \
	-I$(builddir) \
	-DVMOD=1 \
	-D_GNU_SOURCE \
	$(CURL_CFLAGS) \
	$(LIBARCHIVE_CFLAGS)

AM_CFLAGS   = $(VARNISHAPI_CFLAGS) -Wall
AM_CXXFLAGS = $(VARNISHAPI_CFLAGS) -Wall
AM_LDFLAGS  = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS)

if BUILD_FOR_VCP
AM_CPPFLAGS += -DVARNISH_PLUS=1
endif

vmod_LTLIBRARIES = libvmod_kvm.la

if BUILD_FOR_VCP
libvmod_kvm_la_LDFLAGS = \
	-module -export-dynamic -avoid-version -shared \
	-R $(libdir)/varnish-plus $(AM_LDFLAGS)
else
libvmod_kvm_la_LDFLAGS = \
	-module -export-dynamic -avoid-version -shared $(AM_LDFLAGS)
endif

libvmod_kvm_la_SOURCES = \
	kvm_backend.c \
	kvm_live_update.c \
	kvm_post.c \
	to_string.c \
	vmod_kvm.c \
	../tinykvm/vmod_tinykvm_event.c

nodist_libvmod_kvm_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	VSC_vmod_kvm.c \
	VSC_vmod_kvm.h

libvmod_kvm_la_LIBADD = \
	$(KVM_ENGINE_LIB) \
	$(TINYKVM_ENGINE_LIB) \
	$(CURL_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(PTHREAD_LIBS) \
	-lrt

noinst_HEADERS = \
	kvm_backend.h \
	kvm_live_update.h \
	kvm_settings.h \
	vmod_kvm.h

## --- VCC + VSC interface generation --------------------------------------
## The sources #include "vcc_if.h" (CMake's base name), so we generate
## vcc_if.{c,h} explicitly rather than using @BUILD_VMOD_KVM@ (which would
## produce vcc_kvm_if.*). The sources also #include "VSC_vmod_kvm.h", generated
## by vsctool. BUILT_SOURCES forces both headers before any TU is compiled.

BUILT_SOURCES = vcc_if.h VSC_vmod_kvm.h

vcc_if.h vmod_kvm.rst vmod_kvm.man.rst: vcc_if.c

vcc_if.c: $(srcdir)/vmod_kvm.vcc
	$(PYTHON) $(VMODTOOL) -o vcc_if $(srcdir)/vmod_kvm.vcc

VSC_vmod_kvm.h: VSC_vmod_kvm.c
VSC_vmod_kvm.c: $(srcdir)/VSC_vmod_kvm.vsc
	$(PYTHON) $(VSCTOOL) -ch $(srcdir)/VSC_vmod_kvm.vsc

if HAVE_RST2MAN
dist_man_MANS = vmod_kvm.3
vmod_kvm.3: vmod_kvm.man.rst
	$(RST2MAN) vmod_kvm.man.rst $@
endif

## --- tests ---------------------------------------------------------------
AM_TESTS_ENVIRONMENT = \
	PATH="$(VMOD_TEST_PATH):$(PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
## Wrapper injects -Dtestname=<basename> per test (varnishtest has no native
## ${testname} macro; CMake passes it explicitly). See tests/vtc-runner.sh.
VTC_LOG_COMPILER = $(srcdir)/tests/vtc-runner.sh
AM_VTC_LOG_FLAGS = \
	-p vmod_path="$(abs_builddir)/.libs" \
	-Dtopsrc="$(abs_top_srcdir)" \
	-DVMOD_SO="\"$(abs_builddir)/.libs/libvmod_kvm.so\""

TESTS = $(VMOD_KVM_TESTS)
if BUILD_FOR_VCP
TESTS += tests/rotate_vcl.vtc
endif

EXTRA_DIST = \
	vmod_kvm.vcc \
	VSC_vmod_kvm.vsc \
	tests/vtc-runner.sh \
	tests/kvm_api.h \
	kvm_streaming_backend.c \
	$(VMOD_KVM_TESTS) \
	tests/rotate_vcl.vtc

CLEANFILES = \
	vcc_if.c vcc_if.h \
	VSC_vmod_kvm.c VSC_vmod_kvm.h \
	vmod_kvm.rst vmod_kvm.man.rst vmod_kvm.3
