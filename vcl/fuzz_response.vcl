vcl 4.1;
import synthbackend;

backend default none;

sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	unset bereq.http.content-encoding;
	set bereq.method = "POST";
	set bereq.backend = synthbackend.mirror();
	return (fetch);
}
