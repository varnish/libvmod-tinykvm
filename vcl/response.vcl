vcl 4.1;
import synthbackend from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_synthbackend/.libs/libvmod_synthbackend.so";

backend default {
	.path = "/home/gonzo/github/varnish_autoperf/server/server.socket";
    .port = "8081";
}

sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	set bereq.backend = synthbackend.mirror();
}
