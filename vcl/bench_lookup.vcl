vcl 4.1;
import file;

backend default {
	.host = "127.0.0.1";
	.port = "8000";
}

sub vcl_init {
	new f = file.init("/tmp");
}

sub vcl_recv {
//	set req.backend_hint = f.backend();
	return (pass);
}

sub vcl_backend_response {
	set beresp.transit_buffer = 1KB;
}
