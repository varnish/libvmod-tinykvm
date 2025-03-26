# VMOD KVM and TinyKVM

The TinyKVM VMOD processes data in Varnish at native performance using your servers CPU capabilities safely with sandboxing.

## Documentation

You can find the complete [VMOD documentation here](docs/README.md).

## Building and installing

The build dependencies for this VMOD can be found in CI, but briefly:
```
varnish-dev
libcurl4-openssl-dev
libpcre3-dev
libarchive-dev
libjemalloc-dev
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

### Example VCL

```vcl
vcl 4.1;
import tinykvm;
backend default none;

sub vcl_init {
	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");

	# Tell VMOD compute how to contact Varnish (Unix Socket *ONLY*)
	tinykvm.init_self_requests("/tmp/tinykvm.sock");
}

sub vcl_backend_fetch {
	# Fetch a JPG image from a remote server
	tinykvm.chain("fetch",
		"https://filebin.varnish-software.com/tinykvm_programs/spooky.jpg",
		"""{
			"headers": ["Host: filebin.varnish-software.com"]
		}""");
	# Transcode JPG to AVIF and compress with Zstandard
	tinykvm.chain("avif");
	set bereq.backend = tinykvm.program("zstd", "",
		"""{
			"action": "compress",
			"level": 6,
			"resp_headers": ["Content-Disposition: inline; filename=spooky.avif"]
		}""");
}
```

## Example programs

[Programs and examples repository](https://github.com/varnish/tinykvm_examples).

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

