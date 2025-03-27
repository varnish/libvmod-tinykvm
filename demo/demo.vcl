vcl 4.1;
import tinykvm;
backend default none;

sub vcl_init {
	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");
	#tinykvm.library("file:///home/gonzo/github/kvm_demo/compute.json");

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
	# Transcode JPG to AVIF
	set bereq.backend = tinykvm.program("avif", "", """{
		"speed": 10,
		"quality": 35
	}""");
}
