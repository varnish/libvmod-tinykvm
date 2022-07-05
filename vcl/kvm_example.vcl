vcl 4.1;
import kvm;
backend default none;

sub vcl_init {
	kvm.embed_tenants("""{
		"test.com": {
			"key": "123",
			"group": "test",
			"filename": "/tmp/test",

			"max_time": 4.0,
			"max_boot_time": 16.0,
			"max_memory": 512,
			"max_work_memory": 64,
			"hugepages": false
		}
	}""");
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
	if (bereq.method == "POST" && bereq.http.X-LiveUpdate) {
		/* Live update POST */
		set bereq.backend = kvm.live_update(
			bereq.http.Host, bereq.http.X-LiveUpdate, 20MB);
		return (fetch);
	}
	else if (bereq.method == "POST") {
		/* Regular POST */
		set bereq.backend = kvm.vm_post_backend(
			bereq.http.Host,
			bereq.url);
		return (fetch);
	}
	/* Regular request */
	set bereq.backend = kvm.vm_backend(
			bereq.http.Host,
			bereq.url);
}
