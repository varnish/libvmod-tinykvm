vcl 4.1;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
}

sub vcl_recv {
	return (synth(403, "Verboten"));
}

sub vcl_hash {
}

sub vcl_synth {
}
