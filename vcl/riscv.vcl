vcl 4.1;
import riscv;

backend default none;

sub vcl_init {
	/* These functions will be callable on every machine created after */
	riscv.add_known_function("on_client_request");
	riscv.add_known_function("on_synth");

	/* Create some machines */
	new ypizza = riscv.machine(
		name = "ypizza.com",
		filename = "/home/gonzo/github/rvscript/programs/test");
	ypizza.add_known_function("test");
}

sub vcl_recv {
	ypizza.call("on_client_request");

	if (riscv.want_result() == "synth") {
		return (synth(riscv.want_status()));
	}

	return (synth(403, "Verboten"));
}

sub vcl_synth {
	if (riscv.machine_present()) {
		riscv.call("on_synth");
	}
}
