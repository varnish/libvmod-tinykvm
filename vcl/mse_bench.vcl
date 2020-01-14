vcl 4.1;

import mse from "/home/alf/git/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_mse/.libs/libvmod_mse.so";
import std from "/home/alf/git/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_std/.libs/libvmod_std.so";

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}
backend loopback {
        .host = "127.0.0.1";
        .port = "8081";
}

sub vcl_recv
{
        if (std.port(server.ip) == 80)
        {
                set req.http.random  = std.unique_string();
                set req.backend_hint = loopback;
        }
        else {
                set req.http.random = std.random_mod(1000);
        }
}

sub vcl_hash
{
    hash_data(req.http.random);
}

sub vcl_backend_response
{
	if (bereq.url ~ "optane") {
        mse.set_stores("optane");
    } 
	else if (bereq.url ~ "nand") {
           mse.set_stores("nand");
    }
	else {
		mse.set_stores("sata");
	}
	set beresp.ttl = 1w;
}
