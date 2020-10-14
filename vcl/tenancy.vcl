vcl 4.1;
import file;
import riscv;
import std;
import utils;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
	new f = file.init("/home/gonzo");
	riscv.load_tenants(
		"/home/gonzo/github/varnish_autoperf/vcl/tenants.json");
}

sub vcl_recv {

	/* Determine tenant */
	if (req.http.Host) {

		/* Live update mechanism */
		if (req.method == "POST") {
			set req.backend_hint = riscv.live_update(req.http.Host, 15MB);
			std.cache_req_body(15MB);
			return (pass);
		}
		/* Easier to work with wrk */
		if (req.url == "/x") {
			riscv.fork("xpizza.com");
		}
		else if (req.url == "/y") {
			riscv.fork("ypizza.com");
		}
		else if (req.url == "/z") {
			riscv.fork("zpizza.com");
		}
		else if (req.url == "/file") {
			set req.backend_hint = f.backend();
			set req.url = req.url + "?foo=" + utils.fast_random_int(100);
			return (hash);
		}
		/* If fork fails, it's probably not a tenant */
		else if (!riscv.fork(req.http.Host)) {
			return (synth(403));
		}

	} else {
		/* No 'Host: tenant' specified */
		return (synth(403));
	}

	/* Call into VM */
	riscv.fastcall(ON_REQUEST);

	/* Make decision */
	if (riscv.want_result() == "synth") {
		return (synth(riscv.want_status()));
	}
	else if (riscv.want_result() == "backend") {
		set req.http.X-Backend = riscv.want_status();
		return (hash);
	}

	return (synth(403));
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
			riscv.fastcall(ON_SYNTH);
		}
		return (deliver);
	}
}

sub vcl_backend_fetch {
	if (riscv.fork(bereq.http.Host)) {
		riscv.fastcall(ON_BACKEND_FETCH);
		if (bereq.http.X-Backend) {
			set bereq.backend = riscv.vm_backend(bereq.http.X-Backend);
			unset bereq.http.X-Decision;
		}
	}
}

sub vcl_backend_response {
	if (riscv.active()) {
		if (riscv.want_resume()) {
			riscv.resume();
		} else {
			riscv.fastcall(ON_BACKEND_RESPONSE);
		}
	}
}
