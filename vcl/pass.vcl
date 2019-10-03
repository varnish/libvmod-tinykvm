vcl 4.1;
import std;

backend default {
    .path = "/home/gonzo/github/varnish_autoperf/server/server.socket";
	.host = "127.0.0.1";
	.port = "8081";
}
sub vcl_backend_response {
    set beresp.do_esi = true;
}
sub vcl_recv {
	set req.url = req.url + "?foo=" + std.integer(std.random(1, 100), 1);
}
