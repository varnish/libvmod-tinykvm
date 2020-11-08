# Varnish Autoperf
=============================

This repository contains a full CMake build system for Varnish Cache, VMODs and other Varnish related projects that are written in C/C++.

Avoid running `./autogen.des` in the repo folder, as the build system will generate sources in the repo itself, which could affect the CMake build system.

If building vmods is enabled, they will be built into the root build folder with everything else. If you pass `-p vmod_path=$PWD` to Varnish you will be able to import vmods directly like normal: `import std;`, otherwise you will have to import them using an absolute path: `import std from "/abs/path/libvmod_std.so";`.

Dependencies for this build system is:
```
ccache cmake ninja-build libunwind-dev python3
```

## Blazing fast compilation
=============================
- Install ccache
- export CC="ccache clang-11"
- export CXX="ccache clang++-11"
- ./scripts/build.sh

The build programs used are `ccache` and `ninja`, which can be installed with `sudo apt install ccache ninja-build`.

## LTO
=============================
- Not enabled by default, and has no known performance benefits at this time.
- It's usually the case that the more sharing that happens in the kernel the
	more performant the system is. A big benefit of using shared libraries.

## PGO
=============================
- Requires building varnish in single-process mode

## gprof callgraphs
=============================
- Requires building varnish in single-process mode
- See `./scripts/callgraph.sh`.

## AutoFDO
=============================
- Start nodejs server
- Run ./v.sh, which builds and runs a vanilla varnishd.
- Take note of the child PID from varnishd output
- Run ./fdo.sh PID, which starts perf
- Use a HTTP benchmarking tool to generate traffic to varnish
- Ctrl+C
- The auto-profiled varnishd binary will be built

## FlameGraph
=============================
- Start nodejs server
- ./flame.sh
- Use a HTTP benchmarking tool to generate traffic to varnish
- Ctrl+C
- The flamegraph is built as an SVG file
- firefox perf-varnishd.svg
