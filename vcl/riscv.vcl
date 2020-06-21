vcl 4.1;
import riscv;
import std;

backend default none;

sub vcl_init {
	new riscv_backend = riscv.init(max_instructions = 2000000);
}

sub vcl_recv {
	std.cache_req_body(15MB);
	return (hash);
}

sub vcl_backend_fetch {
	set bereq.backend = riscv_backend.from_body();
	return (fetch);
}
