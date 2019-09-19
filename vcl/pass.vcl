vcl 4.1;

backend default {
    .path = "/home/gonzo/github/varnish_autoperf/server/server.socket";
}
sub vcl_backend_response {
    set beresp.do_esi = true;
}
sub vcl_recv {
    return (pass);
}
