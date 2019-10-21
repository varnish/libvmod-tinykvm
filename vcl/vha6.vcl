vcl 4.1;
include "vha6/vha_auto.vcl";

sub vcl_init {
    vha6_opts.set("token", "secret123");
    call vha6_token_init;
}

backend default {
    .host = "localhost";
	.port = "8081";
}
