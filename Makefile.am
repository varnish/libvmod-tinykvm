ACLOCAL_AMFLAGS = -I m4 -I ${VARNISHAPI_DATAROOTDIR}/aclocal

# Build the bundled libkvm engine (static archives) before the VMODs link it.
SUBDIRS = . libkvm-engine src/kvm src/tinykvm

# Install README + LICENSE under $(docdir); the redhat/debian specs package
# %{_docdir}/%{name}/*.
dist_doc_DATA = README.md LICENSE

# Keep the CMake-based developer build shippable inside the dist tarball, and
# ship the bundled engine sources (lib/libkvm, including its ext/* submodules)
# so libkvm-engine's cmake step has something to build from a release tarball.
EXTRA_DIST = \
	get_version \
	build.sh \
	CMakeLists.txt \
	src/config.h.in \
	lib/libkvm \
	demo
