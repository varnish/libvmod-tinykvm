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
	#tinykvm.init_self_requests("", "http://127.0.0.1:8080");

	# Add concurrency to important programs
	tinykvm.configure("avif",
		"""{
			"concurrency": 2,
			"req_mem_limit_after_reset": 20,
			"ephemeral": false
		}""");
	#tinykvm.start("avif");
}
sub vcl_recv {
	if (req.url == "/tcp") {
		tinykvm.steal("hello", req.url);
		return (synth(802));
	}
	else if (req.url == "/invalidate") {
		tinykvm.invalidate_programs();
		return (synth(200));
	}
	else if (req.url == "/" || req.url == "/stats") {
		return (synth(803));
	}
	else if (req.url == "/scounter") {
		return (synth(801));
	}
	else if (req.url == "/chain" || req.url == "/avif/image" || req.url ~ "/cat/") {
		return (hash);
	}
	return (pass);
}

sub vcl_backend_fetch {
	#tinykvm.to_string("collector", bereq.url);

	if (bereq.url == "/avif") {
		# Transform a JPEG asset to AVIF, cache and deliver it.
		tinykvm.chain("fetch", "/avif/image");
		set bereq.backend = tinykvm.program("avif");
	}
	else if (bereq.url == "/avif/image") {
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/tinykvm_programs/rose.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/avif/bench") {
		set bereq.backend = tinykvm.program("avif",
			"""{
				"action": "bench",
				"image": "https://filebin.varnish-software.com/tinykvm_programs/rose.jpg"
			}""");
	}
	else if (bereq.url == "/webp/bench") {
		#tinykvm.invalidate_programs("webp");
		set bereq.backend = tinykvm.program("webp", "/avif/image", "{}");
	}
	else if (bereq.url == "/counter") {
		set bereq.backend = tinykvm.program("counter");
	}
	else if (bereq.url == "/collector") {
		set bereq.backend = tinykvm.program("collector", ":report:");
	}
	else if (bereq.url == "/espeak") {
		# espeak-ng text-to-speech
		if (bereq.http.X-Text) {
			set bereq.backend = tinykvm.program("espeak", "", bereq.http.X-Text);
		} else {
			set bereq.backend = tinykvm.program("espeak", "", "Hello Varnish");
		}
	}
	else if (bereq.url == "/go") {
		#tinykvm.invalidate_programs("go");
		set bereq.backend = tinykvm.program("go", bereq.url, "{}");
	}
	else if (bereq.url == "/gzip") {
		# Decompress a zlib-compressed asset
		set bereq.backend = tinykvm.program("inflate", "http://httpbin.org/gzip");
	}
	else if (bereq.url == "/zstd") {
		# Compress data using zstandard
		tinykvm.chain("zstd", "https://filebin.varnish-software.com/tinykvm_programs/waterfall.avif",
			"""{
				"action": "compress",
				"level": 6,
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
		# Decompress a zstd-compressed asset into AVIF (without content-type)
		set bereq.backend = tinykvm.program("zstd", "",
			"""{
				"action": "decompress",
				"resp_headers": [
					"Content-Disposition: inline; filename=waterfall.avif"
				]
			}""");
	}
	else if (bereq.url == "/min") {
		set bereq.backend = tinykvm.program("minimal");
	}
	else if (bereq.url == "/minify") {
		tinykvm.chain("fetch", "/minify.json");
		set bereq.backend = tinykvm.program("minify");
	}
	else if (bereq.url == "/minify/bench") {
		set bereq.backend = tinykvm.program("minify",
			"""{ "json": "value" }""");
	}
	else if (bereq.url == "/minify.json") {
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/tinykvm_programs/compute.json",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/chain") {
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/tinykvm_programs/723-1200x1200.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url ~ "/chain")
	{
		# curl -D - http://127.0.0.1:8080/chain?tiny
		# curl -D - http://127.0.0.1:8080/chain?small
		# curl -D - http://127.0.0.1:8080/chain?medium
		tinykvm.chain("thumbnails",
			bereq.url,
			"""{
				"sizes": {
					"tiny": 128,
					"small": 256,
					"medium": 512
				}
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "compress"
			}""");
		tinykvm.chain("zstd", bereq.url,
			"""{
				"action": "decompress"
			}""");
		set bereq.backend = tinykvm.program("avif");
	}
	else if (bereq.url == "/none") {
		set bereq.backend = tinykvm.program("none");
	}
	else if (bereq.url == "/string") {
		tinykvm.chain("to_string", "text/plain", "Hello World!");
		tinykvm.chain("zstd", "",
			"""{
				"action": "compress"
			}""");
		set bereq.http.X-Result = tinykvm.to_string("zstd", "",
			"""{
				"action": "decompress"
			}""");
		set bereq.backend =
			tinykvm.program("to_string", "text/plain", bereq.http.X-Result);
	}
	else if (bereq.url == "/ftp") {
		# Fetch something with cURL, and store it in a string. Then pass it to a backend.
		set bereq.backend = tinykvm.program("to_string", "text/plain",
			tinykvm.to_string("fetch", "ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt"));
	}
	else if (bereq.url ~ "^/js") {
		//tinykvm.invalidate_program("jsapp");
		set bereq.backend = tinykvm.program("jsapp", regsub(bereq.url, "^/js", ""));
	}
	else if (bereq.url == "/nim") {
		//tinykvm.invalidate_programs("nim_storage");
		set bereq.backend = tinykvm.program("nim_storage");
	}
	else if (bereq.url == "/nim_prefetch") {
		#tinykvm.invalidate_programs("nim_prefetch_task");
		set bereq.backend = tinykvm.program("nim_prefetch_task", bereq.url, "arg");
	}
	else if (bereq.url == "/nim_prefetch_example") {
		set bereq.backend = tinykvm.program("nim_prefetch_task", bereq.url, "arg");
	}
	else if (bereq.url == "/hello") {
		# Example from documentation
		set bereq.backend = 
			tinykvm.program("to_string", "text/plain", "Hello Compute World!");
		return (fetch);
	}
	else if (bereq.url == "/http3") {
		# Fetch HTTP/3 page with cURL. AltSvc cache enables future fetches to use HTTP/3.
		set bereq.backend = tinykvm.program("fetch", "https://quic.rocks:4433/");
	}
	else if (bereq.url == "/xml") {
		tinykvm.chain("fetch", "http://www.w3schools.com/xml/plant_catalog.xml");

		set bereq.backend = tinykvm.program("xml", "", "{}");
	}
	else if (bereq.url == "/rust") {
		//tinykvm.invalidate_programs("rust");
		set bereq.backend = tinykvm.program("rust", bereq.url);
	}
	else if (bereq.url == "/small_xml") {
		# Pass a small valid XML as response
		set bereq.backend = tinykvm.program("xml", "<a/>", "{}");
	}
	else if (bereq.url == "/bstats") {
		set bereq.backend = tinykvm.program(
			"to_string", "application/json", tinykvm.stats());
	}
	else if (bereq.url == "/sd") {
		set bereq.backend = tinykvm.program("stable_diffusion",
			"A lovely waterfall, high quality", "blurry, ugly, jpeg compression, artifacts, unsharp");
	}
	else if (bereq.url == "/stockfish") {
		tinykvm.invalidate_programs("stockfish");
		set bereq.backend = tinykvm.program("stockfish",
			"""	position startpos move e2e4
				go movetime 1000
			"""
			);
	}
	else if (bereq.url == "/llama") {
		#tinykvm.invalidate_programs("llama");
		set bereq.backend = tinykvm.program("llama",
			bereq.http.X-Prompt, "");
	}
	else if (bereq.url == "/watermark") {
		//tinykvm.invalidate_program("watermark");
		set bereq.backend = tinykvm.program("watermark");
	}
	else if (bereq.url ~ "^/x/reload") {
		tinykvm.invalidate_programs("gbc");
		set bereq.backend = tinykvm.program("gbc", bereq.url);
	}
	else if (bereq.url ~ "^/x") {
		# Gameboy emulator (used by demo page)
		set bereq.backend = tinykvm.program("gbc", bereq.url);
	}
	else if (bereq.url ~ "^/cat/") {
		tinykvm.chain("fetch", "https://http.cat/" + regsub(bereq.url, "^/cat/", ""));
		set bereq.backend = tinykvm.program("avif");
	}
	else {
		tinykvm.chain("fetch", "https://http.cat/404");
		set bereq.backend = tinykvm.program("avif");
	}
}

sub vcl_backend_response {
	if (bereq.url == "/http3" || bereq.url == "/avif" || bereq.url == "/min" || bereq.url == "/minify") {
		set beresp.uncacheable = true;
	}
}

sub vcl_synth {
	if (resp.status == 801) {
		tinykvm.synth(200, "counter");
		return (deliver);
	} else if (resp.status == 802) {
		return (deliver);
	} else if (resp.status == 803) {
		set resp.http.Content-Type = "application/json";
		set resp.body = tinykvm.stats();
		set resp.status = 200;
		return (deliver);
	}
	tinykvm.synth(resp.status, "fetch", "/cat/" + resp.status);
	return (deliver);
}
sub vcl_backend_error {
	tinykvm.synth(beresp.status, "fetch", "/cat/" + beresp.status);
	return (deliver);
}
