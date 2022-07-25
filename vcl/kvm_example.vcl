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
			"allow_make_ephemeral": true
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
		/* Live update POST */
		set bereq.backend = kvm.live_update(
			bereq.http.Host, bereq.http.X-LiveUpdate, 64MB);
		return (fetch);
	}
	/* Regular GET/POST request */
	set bereq.backend = kvm.vm_backend(
			bereq.http.Host,
			bereq.url);
}
sub vcl_backend_response {
	set beresp.uncacheable = true;
}