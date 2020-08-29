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

	new f = file.init("/tmp");

	/* Create some machines */
	new xpizza = riscv.machine(
		name = "xpizza.com",
		filename = "/home/gonzo/github/rvscript/programs/hello_world",
		max_instructions = 88000000);
	new ypizza = riscv.machine(
		name = "ypizza.com",
		filename = "/tmp/pythran",
		max_instructions = 88000000);
	ypizza.add_known_function("my_page");
}

sub vcl_recv {
	/* Live update mechanism */
	if (req.method == "POST") {
		std.cache_req_body(15MB);
		set req.backend_hint = ypizza.live_update();
		return (pass);
	}

	/* Determine tenant and call into VM */
	if (req.url == "/") {
		xpizza.fastcall(ON_REQUEST);
	} else if (req.url == "/file.txt") {
		set req.backend_hint = f.backend();
		return (hash);
	} else if (req.url == "/backend") {
		set req.backend_hint = ypizza.vm_backend("my_page");
		set req.url = req.url + "?" + utils.fast_random_int(80);
		return (hash);
	} else {
		ypizza.fastcall(ON_REQUEST);
	}

	/* Make decision */
	if (riscv.want_result() == "synth") {
		return (synth(riscv.want_status()));
	}
	else if (riscv.want_result() == "backend") {
		set req.backend_hint = riscv.vm_backend();
		return (hash);
	}

	return (synth(403, "Verboten"));
}

sub vcl_hash {
	if (riscv.machine_present()) {
		riscv.fastcall(ON_HASH);
	}
}

sub vcl_synth {
	if (riscv.machine_present()) {
		if (riscv.want_resume()) {
			riscv.resume();
		} else {
			riscv.fastcall(ON_SYNTH);
		}
		return (deliver);
	}
}
