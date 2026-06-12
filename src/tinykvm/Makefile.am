## libvmod_tinykvm -- the high-performance compute VMOD.
##
## Shares the bulk of its glue with libvmod_kvm (../kvm/*.c) and links the same
## libkvm + tinykvm static archives built by ../../libkvm-engine. The VMOD glue
## is C; the libkvm sandbox/runtime is C++17. This Makefile.am lives alongside
## the (untouched) CMakeLists.txt; automake and cmake ignore each other.

KVM_ENGINE_LIB     = $(top_builddir)/libkvm-engine/build/libkvm.a
TINYKVM_ENGINE_LIB = $(top_builddir)/libkvm-engine/build/tinykvm/libtinykvm.a

AM_CPPFLAGS = \
	-I$(srcdir) \
	-I$(builddir) \
	-I$(top_srcdir)/src/kvm \
	-DVMOD=1 \
	-D_GNU_SOURCE \
	$(CURL_CFLAGS) \
	$(LIBARCHIVE_CFLAGS)

AM_CFLAGS   = $(VARNISHAPI_CFLAGS) -Wall -Wno-unused-parameter -Wno-deprecated-declarations
AM_CXXFLAGS = $(VARNISHAPI_CFLAGS) -Wall -Wno-unused-parameter -Wno-deprecated-declarations
AM_LDFLAGS  = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS)

if BUILD_FOR_VCP
AM_CPPFLAGS += -DVARNISH_PLUS=1
endif

vmod_LTLIBRARIES = libvmod_tinykvm.la

if BUILD_FOR_VCP
libvmod_tinykvm_la_LDFLAGS = \
	-module -export-dynamic -avoid-version -shared \
	-R $(libdir)/varnish-plus $(AM_LDFLAGS)
else
libvmod_tinykvm_la_LDFLAGS = \
	-module -export-dynamic -avoid-version -shared $(AM_LDFLAGS)
endif

libvmod_tinykvm_la_SOURCES = \
	../kvm/kvm_backend.c \
	../kvm/kvm_live_update.c \
	../kvm/kvm_post.c \
	../kvm/to_string.c \
	vmod_tinykvm.c \
	vmod_tinykvm_event.c

nodist_libvmod_tinykvm_la_SOURCES = \
	vcc_if.c \
	vcc_if.h \
	VSC_vmod_kvm.c \
	VSC_vmod_kvm.h

libvmod_tinykvm_la_LIBADD = \
	$(KVM_ENGINE_LIB) \
	$(TINYKVM_ENGINE_LIB) \
	$(CURL_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(PTHREAD_LIBS) \
	-lrt

noinst_HEADERS = \
	vmod_tinykvm.h

## --- VCC + VSC interface generation --------------------------------------
## The shared ../kvm/*.c sources #include "vcc_if.h", but vmod_tinykvm.c
## #includes "vcc_tinykvm_if.h" (the name CMake's add_vmod symlinks). We
## compile a single vcc_if.c and provide vcc_tinykvm_if.h as a copy so both
## include names resolve to the same generated interface. The sources also
## #include "VSC_vmod_kvm.h" -- this VMOD reuses libvmod_kvm's VSC schema.

BUILT_SOURCES = vcc_if.h vcc_tinykvm_if.h VSC_vmod_kvm.h

vcc_if.h vmod_tinykvm.rst vmod_tinykvm.man.rst: vcc_if.c

vcc_if.c: $(srcdir)/vmod_tinykvm.vcc
	$(PYTHON) $(VMODTOOL) -o vcc_if $(srcdir)/vmod_tinykvm.vcc

vcc_tinykvm_if.h: vcc_if.h
	cp -f vcc_if.h $@

VSC_vmod_kvm.h: VSC_vmod_kvm.c
VSC_vmod_kvm.c: $(srcdir)/VSC_vmod_kvm.vsc
	$(PYTHON) $(VSCTOOL) -ch $(srcdir)/VSC_vmod_kvm.vsc

if HAVE_RST2MAN
dist_man_MANS = vmod_tinykvm.3
vmod_tinykvm.3: vmod_tinykvm.man.rst
	$(RST2MAN) vmod_tinykvm.man.rst $@
endif

## --- tests ---------------------------------------------------------------
AM_TESTS_ENVIRONMENT = \
	PATH="$(VMOD_TEST_PATH):$(PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = $(srcdir)/tests/vtc-runner.sh
AM_VTC_LOG_FLAGS = \
	-p vmod_path="$(abs_builddir)/.libs" \
	-Dtopsrc="$(abs_top_srcdir)" \
	-DVMOD_SO="\"$(abs_builddir)/.libs/libvmod_tinykvm.so\""

TESTS = $(VMOD_TINYKVM_TESTS)

EXTRA_DIST = \
	vmod_tinykvm.vcc \
	VSC_vmod_kvm.vsc \
	tests/vtc-runner.sh \
	tests/kvm_api.h \
	$(VMOD_TINYKVM_TESTS)

CLEANFILES = \
	vcc_if.c vcc_if.h vcc_tinykvm_if.h \
	VSC_vmod_kvm.c VSC_vmod_kvm.h \
	vmod_tinykvm.rst vmod_tinykvm.man.rst vmod_tinykvm.3
