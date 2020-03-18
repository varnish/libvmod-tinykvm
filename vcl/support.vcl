vcl 4.1;
import std from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_std/.libs/libvmod_std.so";

backend default {
	.path = "/home/gonzo/github/varnish_autoperf/server/server.socket";
    .port = "8081";
}

sub vcl_recv {
	if (req.restarts > 0 && req.http.X-statuscode == "503") {
        set req.url = "/503.html";
        set req.backend_hint = default;
        return (hash); // we will be caching this 503
    }
	return (hash);
}

sub vcl_deliver {
	if (resp.status == 503 && req.restarts == 0) {
        set req.http.X-statuscode = resp.status;    
        return (restart);
    }
}
