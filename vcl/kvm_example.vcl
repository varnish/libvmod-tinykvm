vcl 4.1;
import file;
import kvm;
import std;
backend default none;

sub vcl_init {
	kvm.embed_tenants("""{
		"test.com": {
			"key": "123",
			"group": "test",
			"filename": "/tmp/test",

			"concurrency": 32,
			"max_memory": 512,
			"max_boot_time": 16.0,
			"max_request_memory": 512,
			"max_request_time": 4.0,
			"hugepages": false,
			"ephemeral_hugepages": false,
			"allow_make_ephemeral": true,

			"allowed_paths": [
				"/usr/lib/x86_64-linux-gnu/espeak-ng-data"
			]
		}
	}""");
	new f = file.init(std.getenv("HOME"));
}

sub vcl_recv {
	/* Easier to work with wrk */
	if (req.http.Host ~ "^.+\..+:\d+$") {
		/* This will preserve good Host headers */
		set req.http.Host = "test.com";
	}

	/* Normal request or POST */
}

sub vcl_backend_fetch {
	if (bereq.url == "/file") {
		set bereq.backend = f.backend();
		return (fetch);
	}
	if (bereq.method == "POST" && bereq.http.X-LiveUpdate) {
		if (bereq.http.X-LiveDebug) {
			/* Live update debug POST */
			set bereq.backend = kvm.live_debug(
				bereq.http.Host, bereq.http.X-LiveDebug, 64MB);
			return (fetch);
		} else {
			/* Regular live update POST */
			set bereq.backend = kvm.live_update(
				bereq.http.Host, bereq.http.X-LiveUpdate, 64MB);
			return (fetch);
		}
	}
	if (bereq.http.X-LiveDebug) {
		/* Requests to debug programs */
		set bereq.backend = kvm.vm_debug_backend(
			bereq.http.Host, bereq.http.X-LiveDebug,
			bereq.url);
		return (fetch);
	}

	/* Regular GET/POST request */
	set bereq.backend = kvm.vm_backend(
			bereq.http.Host,
			bereq.url);
}
