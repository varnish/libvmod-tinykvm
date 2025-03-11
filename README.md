# VMOD KVM and TinyKVM

The TinyKVM VMOD processes data in Varnish at native performance using your servers CPU capabilities safely with sandboxing.

## Documentation

No documentation has currently been uploaded yet.

[Documentation here](https://sandbox.varnish-software.com/docs).

## Building and installing

The build dependencies for this VMOD can be found in CI, but briefly:
```
varnish-dev
libcurl4-openssl-dev
libpcre3-dev
libarchive-dev
```

This VMOD does not have an installation procedure. Simply build it from source, and copy the final `libvmod_*.so` into your VMOD folder (usually `/usr/lib/varnish/vmods/`):
```sh
./build.sh
```
Or manually:
```sh
# Create a .build folder and build the VMOD in it
mkdir -p .build
pushd .build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVARNISH_PLUS=OFF
make -j6
popd

# Copy VMODs into Varnish VMOD folder
sudo cp .build/libvmod_*.so /usr/lib/varnish/vmods/
```

If during building you are seeing errors with a Python script, try setting the Python 3 executable directly using a CMake define:
```sh
cmake .. -DCMAKE_BUILD_TYPE=Release -DVARNISH_PLUS=OFF -DPython3_EXECUTABLE=$(which python3)
```

There is a build-merge-release CI system that currently only builds for Ubuntu 22.04:

[Packages here](https://github.com/varnish/libvmod-tinykvm/releases/download/v0.1/artifacts.zip).

## Try it out

[Demonstration VCL here](/demo/tinykvm.vcl). Use `run.sh` in the same directory to try it out.

## Licensing

TinyKVM and VMOD-TinyKVM are released under a dual licensing model:

- **Open Source License**: GPL‑3.0 (see [LICENSE](LICENSE)).
- **Commercial License**: Available under terms controlled by Varnish Software.

For commercial licensing inquiries, please contact:
compliance@varnish-software.com.

## Contributing

We welcome contributions! By submitting a pull request or other contribution,
you agree to our [Contributor License Agreement](CONTRIBUTOR_LICENSE_AGREEMENT.md)
and our [Code of Conduct](CODE_OF_CONDUCT.md).

For details on how to contribute, please refer to this document.

