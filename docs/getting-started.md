# Getting started

## Installation

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

Once the VMOD is installed, you will be able to `import tinykvm;` in VCL.

## Hello World

An example `Hello World!` VCL configuration:
```vcl
import tinykvm;

sub vcl_init {
	# Make the Varnish Enterprise public compute library available
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");
}

sub vcl_backend_fetch {
	# Run the to_string program on /hello
	if (bereq.url == "/hello")
	{
		set bereq.backend =
			tinykvm.program("to_string", "text/plain", "Hello Compute World!");
		return (fetch);
	}
}
```

Verify the output:
```sh
$ curl -D - http://localhost:6081/hello
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 20
Last-Modified: Tue, 22 Aug 2023 08:41:27 GMT
Date: Tue, 22 Aug 2023 08:41:27 GMT
X-Varnish: 2
Age: 0
Via: 1.1 varnish (Varnish/6.0)
Accept-Ranges: bytes
Connection: keep-alive

Hello Compute World!
```
Replace 6081 with the port your Varnish is listening to.

## Chaining together programs

An example `Hello World!` VCL configuration, where we also chain together programs:
```vcl
vcl 4.1;
import tinykvm;

backend default none;

sub vcl_init {
	# Make the Varnish Enterprise public compute library available
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");
}

sub vcl_backend_fetch {
	# Run the to_string program on /hello
	if (bereq.url == "/hello")
	{
		tinykvm.chain("to_string", "text/plain", "Hello Compute World!");

		tinykvm.chain("zstd", "",
			"""{
				"action": "compress"
			}""");

		set bereq.backend =
			tinykvm.program("zstd", "",
				"""{
					"action": "decompress"
				}""");

		return (fetch);
	}
}
```

Here we have a chain of "Hello Compute World!" -> Zstd compress -> Zstd decompress. The result should be the original string!

The final program in the chain is always the call to `tinykvm.program()`.

## Verification

```sh
$ curl -D - http://127.0.0.1:8080/hello
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 20
Last-Modified: Wed, 23 Aug 2023 15:35:33 GMT
Date: Wed, 23 Aug 2023 15:35:33 GMT
X-Varnish: 2
Age: 0
Via: 1.1 varnish (Varnish/6.0)
Accept-Ranges: bytes
Connection: keep-alive

Hello Compute World!
```

Using this method we can compose assets from many operations together. As an example chain that is more realistic we could resize an image, watermark it and then transcode it to WebP or AVIF.
