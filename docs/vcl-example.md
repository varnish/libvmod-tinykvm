# VCL Example

Using the TinyKVM VMOD is fairly straight-forward:

1. Load a library of programs.
2. Read documentation about each individual program in order to understand usage and how to configure the program to do your bidding.
3. Make use of any combination of programs in VCL, eg. passing images into transcoder programs.


## Example image transcoding

```vcl
import tinykvm;

sub vcl_init {
	# Download and activate an example library of compute programs.
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");

	# Tell VMOD TinyKVM how to contact Varnish (Unix Socket *ONLY*).
	tinykvm.init_self_requests("/tmp/tinykvm.sock");
}

sub vcl_backend_fetch {
	if (bereq.url ~ "^/images") {
		# Make a self-request, fetching the image through Varnish.
		tinykvm.chain("fetch", "/backend" + bereq.url);
		# POST the image to the AVIF transcoder, producing image/avif
		set bereq.backend = tinykvm.program("avif");
	}
	else if (bereq.url ~ "^/backend/images") {
		set bereq.backend = ...;
	}
}
```


## URL collection example

```vcl
import tinykvm;

sub vcl_init {
	# Download and activate an example library of compute programs.
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");

	# Tell VMOD TinyKVM how to contact Varnish (Unix Socket *ONLY*).
	tinykvm.init_self_requests("/tmp/tinykvm.sock");
}

sub vcl_recv {
	# Collect every URL that passes through server.
	tinykvm.to_string("collector", req.url);
}

sub vcl_backend_fetch {
	if (bereq.url == "/collector") {
		# Display each URL that passed through server.
		set bereq.backend = tinykvm.program("collector", ":report:");
	}
}
```


## Custom program example

```vcl
import tinykvm;

sub vcl_init {
	# Tell VMOD TinyKVM how to contact Varnish (Unix Socket *ONLY*).
	tinykvm.init_self_requests("/tmp/tinykvm.sock");

	# Create new program inline in VCL. 'test' is a built-in group.
	tinykvm.configure("my_program",
		"""{
			"group" : "test",
			"uri": "file:///home/my_user/github/my_program/my_program.elf"
		}""");
}

sub vcl_backend_fetch {
	# Utilize the new program.
	set bereq.backend = tinykvm.program("my_program", bereq.url);
}
```
