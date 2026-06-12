vcl 4.1;
import tinykvm;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
	.connect_timeout = 1s;
}

sub vcl_init {
	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	#tinykvm.library("https://filebin.varnish-software.com/4wbvu68xy1epbuzv/compute.json");
	tinykvm.library("file:///home/gonzo/github/kvm_demo/compute.json");

	# Tell VMOD compute how to contact Varnish (Unix Socket *ONLY*)
	tinykvm.init_self_requests("/tmp/tinykvm.sock");

	# Add concurrency to important programs
	tinykvm.configure("avif",
		"""{
			"concurrency": 64,
			"ephemeral": true
		}""");
	#tinykvm.start("avif");

    tinykvm.configure("golang",
        """{
			"uri": "https://filebin.varnish-software.com/4wbvu68xy1epbuzv/goexample",
			"address_space": 1800,
			"remapping": ["0xC000000000", 256],
			"max_memory": 64,
			"concurrency": 4,
			"storage": true
		}""");
	//tinykvm.start("golang");

    //tinykvm.library("https://filebin.varnish-software.com/4wbvu68xy1epbuzv/varnish-deno.json");
	tinykvm.library("file:///home/gonzo/github/kvm_demo/deno/varnish-deno.json");
	#tinykvm.start("deno");

    tinykvm.configure("python",
        """{
            "filename": "/home/gonzo/github/kvm_demo/python/python_tinykvm",
			"concurrency": 4,
			"ephemeral": false,
			"main_arguments": [
				"/home/gonzo/github/kvm_demo/python/program.py"
			],
			"current_working_directory": "/home/gonzo/github/kvm_demo/python",
            "allowed_paths": [
				{
					"real": "/lib/x86_64-linux-gnu",
					"prefix": true
				}, {
					"real": "/home/gonzo/github/kvm_demo/python",
					"prefix": true
				}
            ],
			"environment": [
				"PYTHONHOME=/home/gonzo/github/kvm_demo/python/portable-python-cmake-buildsystem/.build",
				"PYTHONPATH=/home/gonzo/github/kvm_demo/python/portable-python-cmake-buildsystem/.build/lib/python3.13"
			]
        }""");
	#tinykvm.start("python");

	# Some programs require startup arguments
	tinykvm.main_arguments("v8", """
		function fibonacci(n) {
		return n < 1 ? 0
				: n <= 2 ? 1
				: fibonacci(n - 1) + fibonacci(n - 2)
		}

		//print("Version: " + version())
		//"fibonacci(20): " + fibonacci(20)
		"Hello Varnish"
	""");
	#tinykvm.start("v8");
}
sub vcl_recv {
	if (req.http.X-JS) {
		set req.http.X-JS = tinykvm.to_string("deno", req.url,
		""" function fibonacci(n) {
			return n < 1 ? 0
					: n <= 2 ? 1
					: fibonacci(n - 1) + fibonacci(n - 2)
			}

			//Math.random = function() {
			//	return 4;
			//};
			//console.log("Math.random() = " + Math.random());

			//for (const [key, value] of req.headers) {
			//	console.log(`JS Header ${key}: ${value}`);
			//}

			//let stuff = await fetch("https://localhost:8080/");
			//const text = new TextEncoder().encode(await stuff.text());
			//await new Promise(resolve => setTimeout(resolve, 100))

			// We can get/set from HTTP request
			const headerValue = req.headers.get("x-js");

			// These will be forwarded to backend_fetch (bereq)
			http_set_req("X-Deno", `Hello from Deno URL=${req.url}`);
			http_set_req("X-Deno2", `Method=${req.method} random=${Math.random()}`);
		""", "fail");
		if (req.http.X-JS == "fail") {
			// JavaScript failed!?
			return (synth(805, "Deno to_string() failed"));
		}
	}

	if (req.http.Upgrade) {
		# Use KVM epoll backend
		return (pipe);
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
	else if (req.url == "/sdeno") {
		return (synth(804));
	}
	else if (req.url == "/chain" || req.url == "/avif/image" || req.url == "/data/cached" || req.url == "/data/do_gzip" || req.url ~ "/cat/") {
		return (hash);
	}
	return (pass);
}

sub vcl_backend_fetch {
	if (bereq.url == "/python") {
		set bereq.backend = tinykvm.program("python", bereq.url, "{}");
	}
	else if (bereq.url == "/avif") {
		# Transform a JPEG asset to AVIF, cache and deliver it.
		tinykvm.chain("fetch", "/avif/image");
		set bereq.backend = tinykvm.program("avif");
	}
	else if (bereq.url == "/avif/image") {
		# Fetch a JPEG asset
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/tinykvm_programs/rose.jpg",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/avif/bench") {
		set bereq.backend = tinykvm.program("avif", "",
			"""{
				"action": "benchmark",
				"quality": 50,
				"speed": 10
			}""");
	}
	else if (bereq.url == "/data/cached") {
		set bereq.backend = tinykvm.program("payload", "",
			"""{
				"size": 10000000,
				"randomize": true
			}""");
	}
	else if (bereq.url == "/data/do_gzip") {
		//set bereq.backend = tinykvm.program("fetch", "/data/cached");
		set bereq.backend = tinykvm.program("payload", "",
			"""{
				"size": 10000000,
				"randomize": true
			}""");
	}
	else if (bereq.url == "/webp/bench") {
		#tinykvm.invalidate_programs("webp");
		tinykvm.chain("fetch", "/avif/image");
		set bereq.backend = tinykvm.program("webp", "",
			"""{
				"quality": 75
			}""");
	}
	else if (bereq.url == "/webp/test") {
		set bereq.backend = tinykvm.program("fetch", "/avif/image");
	}
	else if (bereq.url == "/counter") {
		set bereq.backend = tinykvm.program("counter");
	}
	else if (bereq.url == "/shm_counter") {
		set bereq.backend = tinykvm.program("shared_counter");
	}
	else if (bereq.url == "/collector") {
		set bereq.backend = tinykvm.program("collector", ":report:");
	}
	else if (bereq.url == "/deflate") {
		# Fetch a cached image
		tinykvm.chain("payload", "",
			"""{
				"size": 10000000,
				"randomize": true
			}""");
		# Compress data using libdeflate
		set bereq.backend = tinykvm.program("deflate", "",
			"""{
				"action": "compress",
				"level": 1
			}""");
	}
	else if (bereq.url == "/pdeflate") {
		# Decompress data using libdeflate
		set bereq.backend = tinykvm.program("deflate", "",
			"""{
				"action": "decompress",
				"level": 1
			}""");
	}
	else if (bereq.url == "/zlibng") {
		# Fetch a cached image
		tinykvm.chain("payload", "",
			"""{
				"size": 10000000,
				"randomize": true
			}""");
		# Compress data using zlib-ng
		set bereq.backend = tinykvm.program("zlibng", "",
			"""{
				"action": "compress",
				"level": 1
			}""");
	}
	else if (bereq.url == "/deno") {
		set bereq.backend = tinykvm.program("deno", bereq.url, "{}");
	}
	else if (bereq.url == "/invdeno") {
		tinykvm.invalidate_programs("deno");
		set bereq.backend = tinykvm.program("deno", bereq.url, "{}");
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
		#tinykvm.invalidate_programs("golang");
		set bereq.backend = tinykvm.program("golang", bereq.url, "{}");
	}
	else if (bereq.url == "/gzip") {
		# Decompress a zlib-compressed asset
		set bereq.backend = tinykvm.program("inflate", "http://httpbin.org/gzip");
	}
	else if (bereq.url == "/zstd") {
		# Compress data using zstandard
		tinykvm.chain("zstd", "https://filebin.varnish-software.com/4wbvu68xy1epbuzv/waterfall.avif",
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
	else if (bereq.url == "/minify.json") {
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/4wbvu68xy1epbuzv/compute.json",
			"""{
				"headers": ["Host: filebin.varnish-software.com"]
			}""");
	}
	else if (bereq.url == "/minify") {
		tinykvm.chain("fetch", "https://filebin.varnish-software.com/4wbvu68xy1epbuzv/compute.json");
		set bereq.backend = tinykvm.program("minify");
	}
	else if (bereq.url == "/minify/bench") {
		set bereq.backend = tinykvm.program("minify",
			"""{ "json": "value" }""");
	}
	else if (bereq.url == "/chain") {
		set bereq.backend = tinykvm.program("fetch", "https://filebin.varnish-software.com/4wbvu68xy1epbuzv/723-1200x1200.jpg",
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
	else if (bereq.url == "/rust") {
		set bereq.backend = tinykvm.program("rust", bereq.url, "{}");
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
			tinykvm.program("hello", bereq.url, "Hello Varnish");
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
	else if (bereq.url == "/small_xml") {
		# Pass a small valid XML as response
		set bereq.backend = tinykvm.program("xml", "<a/>", "{}");
	}
	else if (bereq.url == "/bstats") {
		set bereq.backend = tinykvm.program(
			"to_string", "application/json", tinykvm.stats());
	}
	else if (bereq.url == "/stockfish") {
		tinykvm.invalidate_programs("stockfish");
		set bereq.backend = tinykvm.program("stockfish",
			"""	position startpos move e2e4
				go movetime 1000
			"""
			);
	}
	else if (bereq.url == "/v8") {
		set bereq.backend = tinykvm.program("v8");
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
	else if (bereq.url == "/update") {
		set bereq.backend = tinykvm.live_update(bereq.http.Host, bereq.http.X-LiveUpdate);
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
	if (bereq.url == "/data/do_gzip") {
		set beresp.do_gzip = true;
		set beresp.uncacheable = true;
		set beresp.do_stream = false;
	}
	if (bereq.url == "/webp/test") {
		set beresp.uncacheable = true;
		set beresp.do_stream = false;
		#image.webp();
	}
	if (bereq.http.X-Deno) {
		set beresp.http.X-Deno   = bereq.http.X-Deno;
		set beresp.http.X-Deno2  = bereq.http.X-Deno2;
	}
}

sub vcl_synth {
	if (req.http.X-Deno) {
		set resp.http.X-Deno = req.http.X-Deno;
		set resp.http.X-Deno2 = req.http.X-Deno2;
	}
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
	else if (resp.status == 804) {
		# status=0 means defer to the JavaScript program
		tinykvm.synth(0, "deno", "/sdeno",
			"""
			//console.log("Hello from Deno synth");
			""", soft_reset=0);
		return (deliver);
	}
	else if (resp.status == 805) {
		set resp.http.Content-Type = "text/plain; charset=utf-8";
		set resp.body = "Deno JavaScript failed";
		set resp.status = 500;
		return (deliver);
	}
	tinykvm.synth(resp.status, "fetch", "/cat/" + resp.status);
	return (deliver);
}
sub vcl_backend_error {
	tinykvm.synth(beresp.status, "fetch", "/cat/" + beresp.status);
	return (deliver);
}
sub vcl_deliver {
	if (resp.http.X-Deno) {
		set resp.http.X-DenoOut = tinykvm.to_string("deno", req.url,
		"""
			http_set_resp("X-DenoOut1", `Hello from Deno in vcl_deliver`);
		""", "fail", soft_reset=0);
		if (resp.http.X-DenoOut == "fail") {
			// JavaScript failed!?
			return (synth(805, "Deno to_string() failed"));
		}
		unset resp.http.X-DenoOut;
	}
}
