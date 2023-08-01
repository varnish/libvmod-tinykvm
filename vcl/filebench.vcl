vcl 4.1;
import file;
import std;
import utils;

backend default none;

sub vcl_init {
	new f = file.init("/tmp");
}

sub vcl_recv {

	if (req.url == "/file") {
		set req.backend_hint = f.backend();
		set req.http.Host = "file";
		return (pass);
	}
	else if (req.url == "/fbench" || req.url == "/vanilla") {
		set req.backend_hint = f.backend();
		set req.http.Host = "file";
		set req.url = "/vanilla?foo=" + utils.fast_random_int(100);
		return (hash);
	}

	/* Normal request or POST */
	return (pass);
}
