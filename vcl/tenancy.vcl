vcl 4.1;
import riscv;
import std;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
	riscv.load_tenants(
		"/home/gonzo/github/varnish_autoperf/vcl/tenants.json");
}

sub vcl_recv {

	/* Determine tenant */
	if (req.http.Host) {

		/* Live update mechanism */
		if (req.method == "POST") {
			set req.backend_hint = riscv.live_update(req.http.Host);
			std.cache_req_body(15MB);
			return (pass);
		}
		/* If fork fails, it's probably not a tenant */
		if (!riscv.fork(req.http.Host)) {
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
		set req.backend_hint = riscv.vm_backend();
		return (hash);
	}

	return (synth(403));
}

sub vcl_hash {
	if (riscv.active()) {
		riscv.fastcall(ON_HASH);
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
