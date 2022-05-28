vcl 4.1;
import kvm;
import std;

backend default none;

sub vcl_init {
	kvm.embed_tenants("""
		{
			"hugepages": {
				"max_time": 4.0,
				"max_boot_time": 16.0,
				"max_memory": 256,
				"max_work_memory": 8,
				"hugepages": true
			},
			"vpizza.com": {
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"wpizza.com": {
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"xpizza.com": {
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"ypizza.com": {
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"zpizza.com": {
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			}
		}
	""");
}

sub vcl_recv {
	/* Easier to work with wrk */
	if (req.url ~ "^/x") {
		set req.http.Host = "xpizza.com";
	}
	else if (req.url ~ "^/y") {
		set req.http.Host = "ypizza.com";
	}
	else if (req.url ~ "^/z") {
		set req.http.Host = "zpizza.com";
	}
	else if (req.url == "/v") {
		set req.http.Host = "vpizza.com";
	}
	else if (req.url == "/w") {
		set req.http.Host = "wpizza.com";
	}
	else if (req.http.Host ~ "^.+\..+:\d+$") {
		/* This will preserve good Host headers */
		set req.http.Host = "vpizza.com";
	}

	/* Normal request or POST */
}

sub vcl_backend_fetch {
	if (bereq.method == "POST" && bereq.http.X-PostKey) {
		/* Live update POST */
		set bereq.backend = kvm.live_update(
			bereq.http.Host, bereq.http.X-PostKey, 20MB);
		return (fetch);
	}
	else if (bereq.http.Host == "file") {
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

sub vcl_backend_response {
	if (bereq.http.X-KVM-Front) {
		set beresp.http.X-KVM-Front = bereq.http.X-KVM-Front;
	}
}
