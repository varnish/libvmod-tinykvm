# Building and testing VMODs

## Building a VMOD

```
mkdir -p build
cd build
cmake ..
```

If you are using Varnish Plus, enable the VARNISH_PLUS CMake option.

## Running tests

```
cd build
make
ctest --verbose
```
You will have to run make to update the build system if you added more tests, or changed something in the code. Make sure your PATH points to `/opt/varnish` bin and sbin folders. Example:

```
export PATH=/opt/varnish/bin:/opt/varnish/sbin:$PATH
```

## Writing a test

You can write tests as normal, however to import the vmod you will have to write `import vmod from ${VMOD_SO}`.
