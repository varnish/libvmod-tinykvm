vcl 4.1;
import file;
import riscv;
import utils;

backend default none;

sub vcl_init {
	riscv.embed_tenants("""{
		"test.com": {
			"filename": "/tmp/riscv_test",
			"group": "test"
		},
		"mandelbrot.com": {
			"filename": "/tmp/riscv_mandelbrot",
			"group": "test"
		}
	}""");

	new f = file.init("/tmp");
}

sub vcl_recv {
	set req.backend_hint = f.backend();
	set req.http.Host = "file";
	#set req.url = "/vanilla?foo=" + utils.fast_random_int(100);

	if (false) {
	set req.http.X-1 = "Hello-1";
	set req.http.X-2 = "Hello-2";
	set req.http.X-3 = "Hello-3";
	set req.http.X-4 = "Hello-4";
	set req.http.X-5 = "Hello-5";
	set req.http.X-6 = "Hello-6";
	set req.http.X-7 = "Hello-7";
	set req.http.X-8 = "Hello-8";
	set req.http.X-9 = "Hello-9";
	}

	return (hash);
}

sub vcl_backend_fetch {
	if (bereq.url == "/vanilla") {
		return (fetch);
	}

	if (riscv.fork(bereq.http.Host, bereq.http.X-Debug)) {
		riscv.vcall(ON_BACKEND_FETCH);
		set bereq.backend = riscv.vm_backend(
				bereq.http.X-Backend-Func,
				bereq.http.X-Backend-Arg);
	}
}

sub vcl_backend_response {
	if (false) {
	if (bereq.url == "/vanilla") {
		set beresp.http.X-1 = bereq.http.X-1;
		set beresp.http.X-2 = bereq.http.X-2;
		set beresp.http.X-3 = bereq.http.X-3;
		set beresp.http.X-4 = bereq.http.X-4;
		set beresp.http.X-5 = bereq.http.X-5;
		set beresp.http.X-6 = bereq.http.X-6;
		set beresp.http.X-7 = bereq.http.X-7;
		set beresp.http.X-8 = bereq.http.X-8;
		set beresp.http.X-9 = bereq.http.X-9;
	}
	}
}