## CMake build system for VMOD example

This is a proof-of-concept build system for vmod_example.

To build:
```
mkdir -p build && pushd build && cmake .. && make -j4 && popd
```

The build system tries to mimic what the automake system does:
1. Provide a simple function to define a complete vmod.
	- add_vmod(libname libname.vcc "Libname VMOD")
2. Provide functions to add tests
	- vmod_add_tests(libname list.vtc of.vtc tests.vtc)
3. TODO: Other stuff?

The tests don't have a working target yet, but the function should be OK.
