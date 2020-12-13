vcl 4.1;
import synthbackend;

backend default none;

sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	set bereq.backend = synthbackend.mirror();
}
