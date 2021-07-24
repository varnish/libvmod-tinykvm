vcl 4.1;
import kvm;
import std;
import urlplus;
import utils;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
	kvm.embed_tenants("""
		{
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
	if (req.url ~ "/x") {
		set req.http.Host = "xpizza.com";
	}
	else if (req.url == "/y") {
		set req.http.Host = "ypizza.com";
	}
	else if (req.url == "/z") {
		set req.http.Host = "zpizza.com";
	}

	//set req.url = req.url + "?foo=" + utils.cpu_id();
	//set req.url = req.url + "?foo=" + utils.thread_id();
	//set req.url = req.url + "?foo=" + utils.fast_random_int(100);

	/* Determine tenant */
	if (req.http.Host) {
		return (pass);
	} else {
		/* No 'Host: tenant' specified */
		return (synth(403, "Missing Host header field"));
	}
}

sub vcl_backend_fetch {
	set bereq.backend = kvm.vm_backend(
			bereq.http.Host,
			urlplus.get_basename(),
			bereq.url);
}
