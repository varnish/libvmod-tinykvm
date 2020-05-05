vcl 4.1;

#import accept from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_accept.so";
import std    from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_std.so";
import debug from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_debug.so";

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_init {
}

sub vcl_recv {
	#f.write("crash.bin", req.http.Input);
	# x/64bs 0x6310002328d8
	
	set req.http.x-duration = std.duration(req.http.Input, 10d);
	#std.syslog(0, req.http.Input);
	set req.http.x-time     = std.time(req.http.Input, now);
	set req.http.x-real = std.real(req.http.Input, 1.0);
	set req.http.x-sort = std.querysort(req.http.Input);
	std.fnmatch("/foo/[!0-9]", req.http.Input);
	set req.http.x-ip = std.ip(req.http.Input, "1.1.1.1");
	set req.http.x-integer = std.integer(req.http.Input, 1);
	std.timestamp(req.http.Input);
	return (hash);
}

sub vcl_miss {	
	//return (restart);
}
