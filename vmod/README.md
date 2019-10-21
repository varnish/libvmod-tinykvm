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
ctest --verbose
```

Make sure your PATH points to /opt/varnish bin folders. Example:

```
export PATH=/opt/varnish/bin:/opt/varnish/sbin:$PATH
```
