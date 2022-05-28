vcl 4.1;
import file;
import goto;
import riscv;
import kvm;
import std;
import utils;

backend default none;

sub vcl_init {
	new f = file.init(std.getenv("HOME"));
	riscv.embed_tenants("""
		{
			"vpizza.com": {
				"filename": "/tmp/riscv_vpizza",
				"group": "test"
			},
			"xpizza.com": {
				"filename": "/tmp/riscv_xpizza",
				"group": "test"
			},
			"ypizza.com": {
				"filename": "/tmp/riscv_ypizza",
				"group": "test"
			},
			"zpizza.com": {
				"filename": "/tmp/riscv_zpizza",
				"group": "test"
			}
		}
	""");
	kvm.embed_tenants("""
		{
			"vpizza.com": {
				"filename": "/tmp/vpizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test"
			},
			"ypizza.com": {
				"filename": "/tmp/ypizza",
				"key": "12daf155b8508edc4a4b8002264d7494",
				"group": "test",
				"allowed_paths": [
					"/usr/lib/x86_64-linux-gnu/espeak-ng-data/espeak-ng-data",
					"/usr/lib/x86_64-linux-gnu/espeak-ng-data",
					"/usr/lib/locale/locale-archive",
					"/usr/share/locale/locale.alias",
					"/usr/lib/locale/C.utf8/LC_CTYPE",
					"/usr/lib/locale/C.UTF-8/LC_CTYPE"
				]
			}
		}
	""");
}

sub vcl_recv {

	if (req.url == "/file") {
		set req.backend_hint = f.backend();
		set req.url = req.url + "?foo=" + utils.cpu_id();
		//set req.url = req.url + "?foo=" + utils.fast_random_int(100);
		return (hash);
	}
	/* Easier to work with wrk */
	if (req.url ~ "/x") {
		set req.http.Host = "xpizza.com";
		//set req.url = req.url + "?foo=" + utils.cpu_id();
		//set req.url = req.url + "?foo=" + utils.thread_id();
		//set req.url = req.url + "?foo=" + utils.fast_random_int(100);
	}
	else if (req.url ~ "/y") {
		set req.http.Host = "ypizza.com";
	}
	else if (req.url == "/z") {
		set req.http.Host = "zpizza.com";
		set req.url = req.url + "?foo=" + utils.cpu_id();
		//set req.url = req.url + "?foo=" + utils.fast_random_int(100);
	}

	/* Determine tenant */
	if (req.http.Host) {

		/* Live update mechanism */
		if (req.method == "POST") {
			std.cache_req_body(15MB);
			return (pass);
		}
		/* If fork fails, it's probably not a tenant */
		if (!riscv.fork(req.http.Host, req.http.X-Debug)) {
			return (synth(403, "No such tenant"));
		}

	} else {
		/* No 'Host: tenant' specified */
		return (synth(403, "Missing Host header field"));
	}

	/* Call into VM */
	riscv.vcall(ON_REQUEST);

	/* Make decision */
	if (riscv.want_result() == "synth") {
		return (synth(riscv.want_status()));
	}
	else if (riscv.want_result() == "backend") {
		set req.http.X-Backend-Func = riscv.result_value(1);
		set req.http.X-Backend-Arg  = riscv.result_value(2);
		if (riscv.result_value(0) == 0) {
			return (pass);
		} else {
			return (hash);
		}
	}
	else if (riscv.want_result() == "compute") {
		set req.http.X-KVM-Arg  = riscv.result_as_string(1);
		if (riscv.result_value(0) == 0) {
			return (pass);
		} else {
			return (hash);
		}
	}

	return (synth(403, "No decision made"));
}

sub vcl_hash {
	if (riscv.active()) {
		riscv.apply_hash();
	}
}

sub vcl_synth {
	if (riscv.active()) {
		if (riscv.want_resume()) {
			riscv.resume();
		} else {
			riscv.vcall(ON_SYNTH);
		}
		return (deliver);
	}
}

sub vcl_backend_fetch {
	if (bereq.method == "POST") {
		if (bereq.http.X-KVM && bereq.http.X-PostKey) {
			/* KVM Live update POST */
			set bereq.backend = kvm.live_update(
				bereq.http.Host, bereq.http.X-PostKey, 20MB);
		}
		else if (bereq.url == "/") {
			set bereq.backend = riscv.live_update(
				bereq.http.Host, 15MB);
		} else if (bereq.url == "/debug") {
			set bereq.backend = riscv.live_debug(
				bereq.http.Host, 15MB);
		} else {
			return (fail);
		}
		return (fetch);
	}

	if (bereq.http.X-Backend-Func) {
		if (riscv.fork(bereq.http.Host, bereq.http.X-Debug)) {
			riscv.vcall(ON_BACKEND_FETCH);
			set bereq.backend = riscv.vm_backend(
					bereq.http.X-Backend-Func,
					bereq.http.X-Backend-Arg);
			unset bereq.http.X-Decision;
		}
	}
	else if (bereq.http.X-KVM-Arg) {
		/* Regular request */
		set bereq.backend = kvm.vm_backend(
				bereq.http.Host,
				bereq.url,
				bereq.http.X-KVM-Arg);
	}
}

sub vcl_backend_response {
	if (riscv.active()) {
		if (riscv.want_resume()) {
			riscv.resume();
		} else {
			riscv.vcall(ON_BACKEND_RESPONSE);
		}
	}
}

sub vcl_deliver {
	if (riscv.active()) {
		if (riscv.want_resume()) {
			riscv.resume();
		}
		riscv.vcall(ON_DELIVER);
	}
}
