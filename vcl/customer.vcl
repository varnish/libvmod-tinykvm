vcl 4.1;
import edgestash;
import file;
import utils;

backend default none;

sub vcl_init {
	new f = file.init("/tmp");
}

sub vcl_recv {
	set req.backend_hint = f.backend();
	set req.http.Host = "file";
	#set req.url = "/vanilla?foo=" + utils.fast_random_int(100);

	return (hash);
}

sub vcl_backend_fetch {
}

sub vcl_backend_response {

}