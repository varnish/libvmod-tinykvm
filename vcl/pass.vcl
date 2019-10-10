vcl 4.1;
//import std from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_std/.libs/libvmod_std.so";
import ex from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_example.so";

backend default {
    .path = "/home/gonzo/github/varnish_autoperf/server/server.socket";
	.port = "8081";
}
sub vcl_backend_response {
    set beresp.do_esi = true;
}
sub vcl_recv {
	//set req.url = req.url + "?foo=" + std.integer(std.random(1, 100), 1);
	set req.url = req.url + "?foo=" + ex.hello("Gonzo");
}
