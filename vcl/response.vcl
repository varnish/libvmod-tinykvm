vcl 4.0;

import synthbackend from /home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_synthbackend/.libs/libvmod_synthbackend.so;

sub vcl_recv {
	return (pass);
}

sub vcl_backend_fetch {
	set bereq.backend = synthbackend.mirror();
}
