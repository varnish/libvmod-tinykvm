vcl 4.1;
import riscv;
import kvm;
import std;

backend default none;

sub vcl_init {
	riscv.embed_tenants("""{
		"test.com": {
			"filename": "/tmp/riscv_test",
			"group": "test"
		}
	}""");
	kvm.embed_tenants("""{
		"test.com": {
			"filename": "/tmp/test",
			"key": "123",
			"group": "test"
		}
	}""");
}

sub vcl_recv {
	if (req.http.Host == "127.0.0.1:8080") {
		set req.http.Host = "test.com";
	}

	/* Live update mechanism */
	if (req.method == "POST") {
		std.cache_req_body(15MB);
		return (pass);
	}
	/* If fork fails, it's probably not a tenant */
	if (!riscv.fork(req.http.Host, req.http.X-Debug)) {
		return (synth(403, "Unknown tenant"));
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
	riscv.apply_hash();
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
	if (riscv.want_resume()) {
		riscv.resume();
	} else {
		riscv.vcall(ON_BACKEND_RESPONSE);
	}
}

sub vcl_deliver {
	if (riscv.want_resume()) {
		riscv.resume();
	}
	riscv.vcall(ON_DELIVER);
}
