vcl 4.1;
import brotli;
import file;
import goto;
import http;
import kvm;
import std;
import utils;

backend default none;

sub vcl_init {
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
	kvm.embed_tenants("""
		{
			"jpizza.com": {
				"filename": "/tmp/jpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			}
		}
	""");
//	kvm.load_tenants(std.getenv("HOME") + "/git/varnish_autoperf/vcl/kvm_tenants.json");
	new f = file.init("/tmp");
	brotli.init(BOTH, transcode = true);
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
	else if (req.url ~ "^/j") {
		set req.http.Host = "jpizza.com";
		return (pass);
	}
	else if (req.url == "/v") {
		set req.http.Host = "vpizza.com";
	}
	else if (req.url == "/w") {
		set req.http.Host = "wpizza.com";
	}
	else if (req.url == "/file") {
		set req.backend_hint = f.backend();
		set req.http.Host = "file";
		return (pass);
	}
	else if (req.url == "/fbench") {
		set req.backend_hint = f.backend();
		set req.http.Host = "file";
		set req.url = "/file?foo=" + utils.fast_random_int(100);
		return (hash);
	}
	else if (req.url == "/synth") {
		return (synth(
			kvm.vm_call(req.http.Host, "on_recv", req.url)
		));
	}
	else if (req.http.Host ~ "^.+\..+:\d+$") {
		/* This will preserve good Host headers */
		set req.http.Host = "vpizza.com";
	}

	/* Normal request or POST */
	return (pass);
}

sub vcl_synth {
	if (resp.status == 200) {
		kvm.vm_synth();
		return (deliver);
	}
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
	//brotli.compress();
	//set beresp.do_gzip = true;
}
