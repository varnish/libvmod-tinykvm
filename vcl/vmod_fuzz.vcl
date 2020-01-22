vcl 4.1;

import accept from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_accept.so";

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_recv
{
}
