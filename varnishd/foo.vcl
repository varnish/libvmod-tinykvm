vcl 4.0;

backend default {
	.host = "hitch-tls.org";
	.port = "80";
}

sub vcl_backend_response {
	set beresp.ttl = 2h;
}
