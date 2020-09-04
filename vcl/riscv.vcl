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

	riscv.load_tenants("/home/gonzo/tenants.json");
}

sub vcl_recv {
	/* Live update mechanism */
	if (req.method == "POST") {
		std.cache_req_body(15MB);
		set req.backend_hint = riscv.live_update("ypizza.com");
		return (pass);
	}

	/* Determine tenant and call into VM */
	if (req.url == "/") {
		riscv.fork("xpizza.com");
		riscv.fastcall(ON_REQUEST);
	} else if (req.url == "/file.txt") {
		set req.backend_hint = f.backend();
		return (hash);
	} else if (req.url == "/backend") {
		riscv.fork("ypizza.com");
		set req.backend_hint = riscv.vm_backend("my_page");
		set req.url = req.url + "?" + utils.fast_random_int(80);
		return (hash);
	} else {
		riscv.fork("ypizza.com");
		riscv.fastcall(ON_REQUEST);
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
	set resp.http.X-Tenant = riscv.current_name();
	set resp.http.X-Something = "123";
}

sub vcl_deliver {
	set resp.http.X-Tenant = riscv.current_name();
	set resp.http.X-Something = "123";
}
