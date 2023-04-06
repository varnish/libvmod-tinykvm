vcl 4.1;

import accept;
//import file;
//import cookieplus;
import debug;
import headerplus;
#import jwt;
#import slicer;
import std;
#import str;
#import synthbackend;
#import urlplus;
import uri;


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
	new jwt_reader = jwt.reader();
*/
}

sub test_headerplus {
	headerplus.init(req);
	headerplus.attr_set("Input", "max-age", req.http.Input);
	headerplus.attr_get("Input", req.http.Input);
	headerplus.attr_set("Input", "val", req.http.Input);
	headerplus.attr_set("Input", req.http.Input, req.http.Input);
	headerplus.attr_set(req.http.Input, "val", req.http.Input);
	headerplus.attr_set(req.http.Input, req.http.Input, req.http.Input);
	headerplus.add("Input", req.http.Input);
	headerplus.add(req.http.Input, req.http.Input);
	headerplus.collapse_regex(req.http.Input, req.http.Input, req.http.Input);
	headerplus.split(req.http.Input, req.http.Input);
	headerplus.attr_delete("Input", req.http.Input);
	headerplus.prefix("Input", req.http.Input);
	headerplus.suffix("Input", req.http.Input);
	headerplus.rename("Input", req.http.Input);
	headerplus.from_json(req.http.Input, 1);
	headerplus.keep(req.http.Input);
	headerplus.as_json();
	headerplus.write();
	return (hash);
}

sub test_uri {
    uri.parse(req.http.Input);

    set req.http.location = uri.as_string();
    set req.http.s1 = uri.get_scheme();
    set req.http.s2 = uri.get_userinfo();
    set req.http.s3 = uri.get_host();
    set req.http.s4 = uri.get_port();
    set req.http.s5 = uri.get_path();
    set req.http.s6 = uri.get_query();
    set req.http.s7 = uri.get_fragment();

    uri.set_scheme(req.http.Input);
    uri.set_userinfo(req.http.Input);
    uri.set_host(req.http.Input);
    uri.set_port(req.http.Input);
    uri.set_path(req.http.Input);
    uri.set_query(req.http.Input);
    uri.set_fragment(req.http.Input);
    uri.set_port();

    set req.http.Host = req.http.Input;
    uri.write();
    set req.http.Host = uri.as_string();
    uri.write();
    uri.write();

    return (hash);
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
	urlplus.write();

    set req.http.str1 = str.split(req.http.Input, 2, ", ");
	set req.http.str2 = str.split(req.http.Input, 2, req.http.Input);
	set req.http.str3 = str.substr(req.http.Input, 20);

    #set req.http.r1 = jwt_reader.parse(req.http.Input);
	#set req.http.r2 = jwt_reader.set_key("my secret");
    #set req.http.r3 = jwt_reader.verify("HS256");
*/

	if (false) {
		call test_headerplus;
        call test_uri;
	}

	#set req.http.Range = req.http.Input;
    headerplus.init(req);
	headerplus.attr_count(req.http.Input);

	# DO NOT USE (hash)!!
	return (pass);
	# DO NOT USE (hash)!!
}

sub vcl_miss {
	//return (restart);
}

## SLICER ##
#sub vcl_backend_fetch {
#    if (!slicer.failed()) {
#        slicer.enable();
#    }
#}
#sub vcl_backend_error {
#    if (slicer.failed()) {
#        return (retry);
#    }
#}
