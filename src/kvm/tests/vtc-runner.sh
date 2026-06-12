#!/bin/sh
# varnishtest LOG_COMPILER wrapper for the automake `make check' harness.
#
# A few tests build a per-test guest program into ${tmpdir}/${testname}.* and
# reference it as ${testname}. varnishtest defines ${testdir}/${tmpdir} itself
# but NOT ${testname}; the CMake build injects it per-test via
# `-Dtestname=${TEST}' (see CMakeLists.txt: add_vmod_tests). automake uses a
# single shared LOG_COMPILER, so we derive testname from the .vtc filename
# (passed as the last argument) and inject the same macro here.
set -e

# The test file is the final positional argument.
for testfile; do :; done
testname=$(basename "$testfile" .vtc)

exec varnishtest -v -Dtestname="$testname" "$@"
