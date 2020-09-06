vcl 4.1;
import riscv;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
	riscv.load_tenants("/home/gonzo/tenants.json");
}

sub vcl_recv {

	/* Determine tenant */
	if (req.http.Host) {

		/* Live update mechanism */
		if (req.method == "POST") {
			set req.backend_hint = riscv.live_update(req.http.Host);
			return (pass);
		}

		riscv.fork(req.http.Host);

	} else {
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
