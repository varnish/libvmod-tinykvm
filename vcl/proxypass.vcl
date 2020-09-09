vcl 4.1;
import utils;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_recv {
	return (pass);
}
sub vcl_backend_response {
	utils.set_bypass_buffer_size(2KB);
}
