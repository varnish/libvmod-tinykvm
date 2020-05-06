vcl 4.1;

#import accept from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_accept.so";
import std    from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_std.so";
import blob   from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_blob.so";
import cookie from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_cookie.so";
import debug  from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_debug.so";
import header from "/home/gonzo/github/varnish_autoperf/build_vc/libvmod_header.so";

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_init {
	new myblob   = blob.blob(BASE64, "Zm9vYmFy");
}

sub test_vmod_std {
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
sub test_vmod_header {
	set req.http.X = header.get(req.http.Set-Cookie, req.http.Input);
	header.append(req.http.Set-Cookie, req.http.Input);
	header.regsub(req, req.http.Input, req.http.Input);
	header.remove(req.http.Set-Cookie, req.http.Input);

	header.append(req.http.Input, req.http.Input);
	header.remove(req.http.Input, req.http.Input);
	header.copy(req.http.Input, req.http.Input);
	return (hash);
}
sub test_vmod_cookie {
	cookie.parse(req.http.Input);
	cookie.delete(req.http.Input);
	cookie.keep(req.http.Input);
	set req.http.cookie = cookie.get_string();
	return (hash);
}
sub test_vmod_blob {
	set req.http.Base64-Encoded = blob.encode(HEX,
		blob=blob.decode(BASE64, encoded=req.http.Input));
	set req.http.Base64-Encoded = blob.encode(BASE64,
		blob=blob.decode(HEX, encoded=req.http.Input));
	return (hash);
}

sub vcl_recv {
	#f.write("crash.bin", req.http.Input);
	# x/64bs 0x6310002328d8
	if (false) {
		call test_vmod_std;
		call test_vmod_header;
		call test_vmod_cookie;
	}

	call test_vmod_blob;

	return (hash);
}

sub vcl_miss {	
	//return (restart);
}

sub vcl_backend_response {
	
}
