vcl 4.1;
import riscv;
import std;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
	/* These functions will be callable on every machine created after */
	riscv.add_known_function("on_client_request");
	riscv.add_known_function("on_hash");
	riscv.add_known_function("on_synth");
	riscv.add_known_function("on_backend_fetch");
	riscv.add_known_function("on_backend_response");

	/* Create some machines */
	new xpizza = riscv.machine(
		name = "xpizza.com",
		filename = "/home/gonzo/github/rvscript/programs/hello_world",
		max_instructions = 1000000);
	new ypizza = riscv.machine(
		name = "ypizza.com",
		filename = "/home/gonzo/github/rvscript/programs/test",
		max_instructions = 1000000);
	ypizza.add_known_function("test");
}

sub vcl_recv {
	/* Live update mechanism */
	if (req.method == "POST") {
		std.cache_req_body(15MB);
		set req.backend_hint = ypizza.live_update();
		return (pass);
	}

	if (req.url == "/") {
		xpizza.call_index(0);  /* on_client_request */
	} else {
		ypizza.call_index(0);  /* on_client_request */
	}

	if (riscv.want_result() == "synth") {
		return (synth(riscv.want_status()));
	}

	return (synth(403, "Verboten"));
}

sub vcl_hash {
	if (riscv.machine_present()) {
		riscv.call_index(1); /* on_hash */
	}
}

sub vcl_synth {
	if (riscv.machine_present()) {
		riscv.call_index(2); /* on_synth */
	}
}
