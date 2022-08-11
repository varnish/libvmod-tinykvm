vcl 4.1;

include "/home/gonzo/github/varnish_autoperf/vcl/waf.vcl";
import synthbackend;

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_init {
	varnish_waf.add_files("/home/gonzo/github/varnish_autoperf/vcl/modsecurity.conf");
}

sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	set bereq.backend = synthbackend.mirror();
}
