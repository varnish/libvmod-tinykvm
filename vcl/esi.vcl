vcl 4.1;
import aclplus from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_aclplus/.libs/libvmod_aclplus.so";
import kvstore from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_kvstore/.libs/libvmod_kvstore.so";

backend default {
    .host = "localhost";
    .port = "8081";
}

sub vcl_init {
	new purgers = kvstore.init();
	purgers.init_file("/home/gonzo/github/varnish_autoperf/data.csv", ",");
}

sub vcl_recv {
	if (aclplus.match(client.ip, purgers.get(req.http.host, "error"))) {
		return (synth(405));
	}
}

sub vcl_backend_response {
    set beresp.do_esi = true;
}
