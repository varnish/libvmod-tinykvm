vcl 4.1;
import std;
import utils;

backend default {
        .host = "127.0.0.1";
        .port = "8008";
}
sub vcl_backend_response {
    //set beresp.do_esi = true;
}
sub vcl_recv {
        set req.url = req.url + "?foo=" + std.integer(std.random(1, 10000), 1);
}
