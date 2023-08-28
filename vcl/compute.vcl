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
	compute.library("https://filebin.varnish-software.com/kvmprograms/compute.json");

	# Tell VMOD compute how to contact Varnish (Unix Socket *ONLY*)
	compute.init_self_requests("/tmp/compute.sock/");
	#compute.init_self_requests("", "http://127.0.0.1:8080");

	# Add hugepage support to important programs
	compute.configure("avif",
		"""{
			"hugepages": false,
			"request_hugepages": false,
			"concurrency": 32,
			"ephemeral": true
		}""");
	compute.configure("thumbnails",
		"""{
			"concurrency": 32
		}""");
	compute.configure("zstd",
		"""{
			"concurrency": 32
		}""");
	# Start the JPEG-to-AVIF transcoder, but don't delay Varnish startup.
	compute.start("avif");
}
sub vcl_recv {
	if (req.url == "/tcp") {
		compute.steal("hello", req.url);
		return (synth(404));
	}
	if (req.url == "/invalidate") {
		compute.invalidate_program("*");
		return (synth(200));
	}
	if (req.url == "/chain" || req.url == "/avif/image") {
		return (hash);
	}
	return (pass);
}

sub vcl_backend_fetch {
	if (bereq.url == "/avif") {
		# Transform a JPEG asset to AVIF, cache and deliver it. cURL can fetch using TLS and HTTP/2.
		set bereq.backend = compute.program("avif", "/avif/image", "{}");
	}
	else if (bereq.url == "/avif/image") {
		set bereq.backend = compute.program("fetch", "https://${filebin}/kvmprograms/723-1200x1200.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/avif/bench") {
		set bereq.backend = compute.program("avif");
	}
	else if (bereq.url == "/webp/bench") {
		//compute.invalidate_program("webp");
		set bereq.backend = compute.program("webp");
	}
	else if (bereq.url == "/counter") {
		set bereq.backend = compute.program("counter");
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
		set bereq.backend = compute.program("zstd", "https://${filebin}/kvmprograms/waterfall.png",
			"""{
				"action": "compress",
				"level": 6,
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/min") {
		set bereq.backend = compute.program("minimal");
	}
	else if (bereq.url == "/minify") {
		compute.chain("fetch", "/minify.json");
		set bereq.backend = compute.program("minify");
	}
	else if (bereq.url == "/minify/bench") {
		set bereq.backend = compute.program("minify",
			"""{ "json": "value" }""");
	}
	else if (bereq.url == "/minify.json") {
		set bereq.backend = compute.program("fetch", "https://${filebin}/kvmprograms/compute.json",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/chain") {
		set bereq.backend = compute.program("fetch", "https://${filebin}/kvmprograms/723-1200x1200.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url ~ "/chain")
	{
		compute.chain("thumbnails",
			bereq.url,
			"""{
				"sizes": {
					"tiny": 128,
					"small": 256,
					"medium": 512
				}
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		compute.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		set bereq.backend = compute.program("avif");
	}
	else if (bereq.url == "/none") {
		set bereq.backend = compute.program("none");
	}
	else if (bereq.url == "/string") {
		compute.chain("to_string", "text/plain", "Hello World!");
		compute.chain("zstd", "",
			"""{
				"action": "compress"
			}""");
		set bereq.http.X-Result = compute.to_string("zstd", "",
			"""{
				"action": "decompress"
			}""");
		set bereq.backend =
			compute.program("to_string", "text/plain", bereq.http.X-Result);
	}
	else if (bereq.url == "/ftp") {
		# Fetch something with cURL, and store it in a string. Then pass it to a backend.
		set bereq.backend = compute.program("to_string", "text/plain",
			compute.to_string("fetch", "ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt"));
	}
	else if (bereq.url ~ "^/js") {
		//compute.invalidate_program("jsapp");
		set bereq.backend = compute.program("jsapp", regsub(bereq.url, "^/js", ""));
	}
	else if (bereq.url == "/nim") {
		//compute.invalidate_program("nim_storage");
		set bereq.backend = compute.program("nim_storage");
	}
	else if (bereq.url == "/hello") {
		# Example from documentation
		set bereq.backend = 
			compute.program("to_string", "text/plain", "Hello Compute World!");
		return (fetch);
	}
	else if (bereq.url == "/http3") {
		# Fetch HTTP/3 page with cURL. AltSvc cache enables future fetches to use HTTP/3.
		set bereq.backend = compute.program("fetch", "https://quic.rocks:4433/");
	}
	else if (bereq.url == "/xml") {
		#compute.chain("fetch", "http://www.w3schools.com/xml/plant_catalog.xml");
		compute.chain("fetch", "https://${filebin}/kvmprograms/compute.json");
		set bereq.backend = compute.program("xml", "", "{}");
	}
	else if (bereq.url == "/rust") {
		//compute.invalidate_program("rust");
		set bereq.backend = compute.program("rust", bereq.url);
	}
	else if (bereq.url == "/small_xml") {
		# Pass a small valid XML as response
		set bereq.backend = compute.program("xml", "<a/>", "{}");
	}
	else if (bereq.url == "/sd") {
		set bereq.backend = compute.program("stable_diffusion",
			"A lovely cat, high quality", "blurry, ugly, jpeg compression, artifacts, unsharp");
	}
	else if (bereq.url == "/watermark") {
		compute.invalidate_program("watermark");
		set bereq.backend = compute.program("watermark");
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
	if (bereq.url == "/http3" || bereq.url == "/avif" || bereq.url == "/min" || bereq.url == "/minify") {
		set beresp.uncacheable = true;
	}
}
