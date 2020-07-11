vcl 4.1;
import riscv;

backend default none;

sub vcl_init {
	new machine = riscv.machine(max_instructions = 2000000,
		filename = "/home/gonzo/github/rvscript/programs/test");
}

sub vcl_recv {
	if (machine.call("on_client_request")) {
		return (synth(700, "Looks OK to me"));
	}
	return (synth(400, "Verboten"));
}

sub vcl_synth {
	machine.call("on_synth");
}

sub vcl_backend_response {
	machine.call("on_backend_response");
}
