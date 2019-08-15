vcl 4.1;

backend default {
    .host = "localhost";
    .port = "8081";
}
sub vcl_recv {
    return (pass);
}
