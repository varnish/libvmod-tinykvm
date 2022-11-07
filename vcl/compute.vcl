vcl 4.1;
import activedns;
import compute;

backend default {
	.host = "127.0.0.1";
	.port = "7443";
}

sub vcl_init {
	new filebin = activedns.dns_group();
	filebin.set_host("filebin.varnish-software.com");

	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	compute.library("https://filebin.varnish-software.com/nsyb0c1pvwa7ecf9/compute.json");
	# Add a local program directly (using default group)
	compute.add_program("watermark", "file:///tmp/kvm_watermark");
	# Configure program 'avif' with some overrides
	compute.configure("minimal", """{
        "ephemeral": false,
		"concurrency": 2
	}""");
	compute.configure("minify", """{
        "ephemeral": true,
		"concurrency": 2
	}""");
	# Start the AVIF transcoder, but don't delay Varnish startup.
	#compute.start("avif");
	compute.start("espeak");
	#compute.start("fetch");
	#compute.start("inflate");
	compute.start("minimal");
	#compute.start("zstd");
}
sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	if (bereq.url == "/avif") {
		# Transform a JPEG asset to AVIF, cache and deliver it. cURL can fetch using TLS and HTTP/2.
		set bereq.backend = compute.program("avif", "https://${filebin}/nsyb0c1pvwa7ecf9/rose.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/avif/bench") {
		set bereq.backend = compute.program("avif", "");
	}
	else if (bereq.url == "/espeak") {
		# espeak-ng text-to-speech
		if (bereq.http.X-Text) {
			set bereq.backend = compute.program("espeak", "", bereq.http.X-Text);
		} else {
			set bereq.backend = compute.program("espeak", "", "Hello Varnish");
		}
	}
	else if (bereq.url == "/gzip") {
		# Decompress a zlib-compressed asset
		set bereq.backend = compute.program("inflate", "http://httpbin.org/gzip");
	}
	else if (bereq.url == "/zstd") {
		# Decompress a zstd-compressed asset into a PNG (without content-type)
		set bereq.backend = compute.program("zstd", "http://127.0.0.1:8080/zstd/compress",
			"""{
				"action": "decompress",
				"resp_headers": [
					"Content-Disposition: inline; filename=waterfall.png"
				]
			}""");
	}
	else if (bereq.url == "/zstd/compress") {
		# Compress data using zstandard
		set bereq.backend = compute.program("zstd", "https://${filebin}/nsyb0c1pvwa7ecf9/waterfall.png",
			"""{
				"action": "compress",
				"level": 6,
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/min") {
		set bereq.backend = compute.program("minimal", "");
	}
	else if (bereq.url == "/minify") {
		set bereq.backend = compute.program("minify", "");
	}
	else if (bereq.url == "/none") {
		set bereq.backend = compute.program("none", "");
	}
	else if (bereq.url == "/ftp") {
		# Fetch something with cURL
		set bereq.backend = compute.program("fetch", "ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt");
	}
	else if (bereq.url == "/watermark") {
		set bereq.backend = compute.program("watermark", "");
	}
	else if (bereq.url == "/http3") {
		# Fetch HTTP/3 page with cURL. AltSvc cache enables future fetches to use HTTP/3.
		set bereq.backend = compute.program("fetch", "https://quic.rocks:4433/");
	}
	else if (bereq.url ~ "^/x") {
		# Gameboy emulator (used by demo page)
		set bereq.backend = compute.program("gbc", bereq.url);
	}
	else {
		# All unknown URLs to demo static site
		set bereq.backend = compute.program("demo", bereq.url);
	}
}
sub vcl_backend_response {
	if (bereq.url == "/http3") {
		set beresp.uncacheable = true;
	}
}
