vcl 4.1;
import goto;
import kvm;
import std;
import urlplus;
import utils;

backend default none;

sub vcl_init {
	kvm.embed_tenants("""
		{
			"wpizza.com": {
				"filename": "/tmp/wpizza",
				"group": "test"
			},
			"xpizza.com": {
				"filename": "/tmp/xpizza",
				"group": "test"
			},
			"ypizza.com": {
				"filename": "/tmp/ypizza",
				"group": "test"
			},
			"zpizza.com": {
				"filename": "/tmp/zpizza",
				"group": "test"
			}
		}
	""");
}

sub vcl_recv {

	/* Easier to work with wrk */
	if (req.url == "/x") {
		set req.http.Host = "xpizza.com";
	}
	else if (req.url == "/y") {
		set req.http.Host = "ypizza.com";
	}
	else if (req.url == "/z") {
		set req.http.Host = "zpizza.com";
	}
	else if (req.url == "/w") {
		set req.http.Host = "wpizza.com";
	}
	else if (req.http.Host ~ "^\d+\.\d+\.\d+\.\d+:\d+$") {
		set req.http.Host = "ypizza.com";
	}

	//set req.url = req.url + "?foo=" + utils.cpu_id();
	//set req.url = req.url + "?foo=" + utils.thread_id();
	//set req.url = req.url + "?foo=" + utils.fast_random_int(100);

	/* Determine tenant */
	if (req.method == "POST") {
		if (req.http.X-PostKey == "12daf155b8508edc4a4b8002264d7494") {
			set req.backend_hint = kvm.live_update(req.http.Host, 15MB);
			std.cache_req_body(15MB);
			return (pass);
		} else {
			/* Wrong POST key */
			return (synth(403, "Invalid POST request"));
		}
	}
	return (hash);
}

sub vcl_backend_fetch {
	if (bereq.method == "POST") {
		return (fetch);
	}

	set bereq.backend = kvm.vm_backend(
			bereq.http.Host,
			"my_backend",
			bereq.url);
}
