vcl 4.1;
import brotli;
import file;
import goto;
import kvm;
import std;
import urlplus;
import utils;

backend default none;

sub vcl_init {
	kvm.cache_symbol("my_backend");
	kvm.embed_tenants("""
		{
			"vpizza.com": {
				"filename": "/tmp/vpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"wpizza.com": {
				"filename": "/tmp/wpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"xpizza.com": {
				"filename": "/tmp/xpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"ypizza.com": {
				"filename": "/tmp/ypizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"zpizza.com": {
				"filename": "/tmp/zpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			}
		}
	""");
	new f = file.init("/tmp");
	brotli.init(BOTH, transcode = true);
}

sub vcl_recv {

	/* Easier to work with wrk */
	if (req.url == "/x") {
		set req.http.Host = "xpizza.com";
	}
	else if (req.url == "/xhash") {
		set req.http.Host = "xpizza.com";
		return (hash);
	}
	else if (req.url == "/y") {
		set req.http.Host = "ypizza.com";
	}
	else if (req.url == "/z") {
		set req.http.Host = "zpizza.com";
	}
	else if (req.url == "/v") {
		set req.http.Host = "vpizza.com";
	}
	else if (req.url == "/w") {
		set req.http.Host = "wpizza.com";
	}
	else if (req.url == "/file") {
		set req.backend_hint = f.backend();
		set req.url = "/inn.png";
		return (pass);
	}
	else if (req.url == "/synth") {
		return (synth(700, "Testing"));
	}
	else {
		set req.http.Host = "vpizza.com";
	}

	//set req.url = req.url + "?foo=" + utils.cpu_id();
	//set req.url = req.url + "?foo=" + utils.thread_id();
	//set req.url = req.url + "?foo=" + utils.fast_random_int(100);

	/* Determine tenant */
	if (req.method == "POST") {
		set req.backend_hint = kvm.live_update(
			req.http.Host, req.http.X-PostKey, 20MB);
		std.cache_req_body(20MB);
		return (pass);
	}
	return (pass);
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

sub vcl_backend_response {
	brotli.compress();
	//set beresp.do_gzip = true;
}
