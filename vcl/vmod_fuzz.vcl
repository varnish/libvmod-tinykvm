vcl 4.1;

#import accept from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_accept.so";
#import std    from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_std.so";
#import urlplus from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_urlplus.so";
#import file   from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_file/.libs/libvmod_file.so";
#import cookieplus from "/home/gonzo/github/varnish_autoperf/ext/varnish-cache-plus/lib/libvmod_cookieplus/.libs/libvmod_cookieplus.so";
import jwt from "/home/gonzo/github/varnish_autoperf/vmod/build/libvmod_jwt.so";

backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_init {
/*	new lang = accept.rule("en");
	lang.add("en");
	lang.add("fr");
	lang.add("de");
	lang.add("it");
	lang.add("es");
	lang.add("pl");
	new f = file.init("/home/gonzo/github/varnish_autoperf");
	f.allow("*", "rw");
*/
	new jwt_reader = jwt.reader();
}

sub vcl_recv {
	#f.write("crash.bin", req.http.Input);
	# x/64bs 0x6310002328d8
	
	#set req.http.x-accept-language = lang.filter(req.http.Input);
	#set req.http.x-duration = std.duration(req.http.Input, 10d);
	#std.syslog(0, req.http.Input);
	#set req.http.x-time     = std.time(req.http.Input, now);
	##set req.http.x-real = std.real(req.http.Input, 1.0);
	#set req.http.x-sort = std.querysort(req.http.Input);
	#std.fnmatch("/foo/[!0-9]", req.http.Input);
	#set req.http.x-ip = std.ip(req.http.Input, "1.1.1.1");
	#set req.http.x-integer = std.integer(req.http.Input, 1);
	#std.timestamp(req.http.Input);

	#set req.http.x-cptest = cookieplus.get("test");
	#cookieplus.keep("test");
	#cookieplus.write();
	#set req.http.x-cptest = cookieplus.as_string();

/*	urlplus.parse(req.url);
	set req.http.x-base = urlplus.get_basename();
	set req.http.x-file = urlplus.get_filename();
	set req.http.x-ext = urlplus.get_extension();
	set req.http.x-dir = urlplus.get_dirname();
	set req.http.x-url = urlplus.as_string();
	set req.http.x-query = urlplus.query_get("query");
	urlplus.write();*/

    set req.http.r1 = jwt_reader.parse(req.http.Input);
	set req.http.r2 = jwt_reader.set_key("my secret");
    set req.http.r3 = jwt_reader.verify("HS256");
	return (hash);
}

sub vcl_miss {	
	//return (restart);
}
