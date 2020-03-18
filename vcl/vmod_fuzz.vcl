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
    set req.http.r5 = jwt_reader.get_typ();
    set req.http.r6 = jwt_reader.get_b64();
    set req.http.r7 = jwt_reader.is_jwt();
    set req.http.r8 = jwt_reader.get_sub();
    set req.http.ra = jwt_reader.get_iss();
    set req.http.rb = jwt_reader.get_jti();
    set req.http.rc = jwt_reader.has_exp();
    set req.http.rd = jwt_reader.has_nbf();
    set req.http.re = jwt_reader.get_header();
    set req.http.rf = jwt_reader.get_header_encoded();
    set req.http.rg = jwt_reader.get_payload();
    set req.http.rh = jwt_reader.get_payload_encoded();
    set req.http.ri = jwt_reader.get_signature();
    set req.http.rj = jwt_reader.to_string();
    set req.http.r11 = jwt_reader.set_key({"-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAubte8GHNZdL3GKaFpyfX
P9Pd8XEt50Ad9OtZpA4NAbVzJafoHuHGGl+HtKh82O6D+itrQv2kPRWU8CGAWYOH
61pakNNgnPugBt50ioejjwd60gTbvyHzYRvotRCqB/yUduuVUUlM/35spuoobLIV
rX2LdEs9t/ysLwmKU9zLE/Sc0FKg+QHjRW0tNqRT3YxR0Z+RRaOMJ4Ne5JFrhSwh
Gr1+caDutCoUycvH9k8gj9aWYg8NhHV+0FUl9DlOxp7WQsCI9uEOEzz3IVDtEDd9
pG7auzw6BAMBNzuCurX+BRwxaoS7Ad9KD+aqzBWTbOe6a6NsL5DAdYyfncqVgrpQ
dwIDAQAB
-----END PUBLIC KEY-----"});
    set req.http.r12 = jwt_reader.verify(jwt_reader.get_alg());
    set req.http.r4 = jwt_reader.set_key("hXM2XxmaLHKtJsxnP1N89KKNW3asPCAeg65Tq_xi3-M");
    set req.http.r3 = jwt_reader.verify(jwt_reader.get_alg());
    set req.http.r2 = jwt_reader.set_jwk({"
        {
            "keys": [
                {
                    "alg":"RS256",
                    "n":"123",
                    "e":"456"
                }, {
                    "alg":"RS256",
                    "n":"2rACzRJbg7Upm4OLttb0sRiy8umUS48Bj6IpM_as0EKQez4NGibjLOzh9QpwwQEFS8EeBTKWyL9-VKLAbu1EPjb8YdPz-O1c7lt09XhVhUUHcrRFscLHZ1-dshxXf2Sec0RfF9ufF2kJGxPBdrWZnb1jnA6UyCo6g4BnZSC0I__OMp-9PZz6XWxbBuq3pU3KSIP4ow85FyjL8JfGiSj0Ve-p_bqZoWJ6fK55NCPqZfmruumz1pnCEkizXvUb46zFX4nfkSqtokeLytt6lDpWyX9FWj6j2kueW54j9jY2JFyFK8uin5e2BKslwIpQYJ887P0ue1Y-lCXNYNlC8003kw",
                    "e":"AQAB"
                }, {
                    "alg":"RS256",
                    "n":"123",
                    "e":"456"
                }
            ]
        }
    "});
    set req.http.r9 = jwt_reader.verify(jwt_reader.get_alg());
	return (hash);
}

sub vcl_miss {	
	//return (restart);
}
