vcl 4.1;
//import std from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_std/.libs/libvmod_std.so";

backend default {
    .host = "127.0.0.1";
	.port = "8081";
}
sub vcl_backend_response {
}
sub vcl_recv {
	//set req.url = req.url + "?foo=" + std.integer(std.random(1, 100), 1);
	return (pass);
}
