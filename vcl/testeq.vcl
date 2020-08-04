vcl 4.1;
import std;
import header;

backend default {
	.host = "127.0.0.1";
	.port = "8081";
}

sub vcl_init {
}

sub vcl_recv {
	set req.http.X-VM = "Client request";
	set req.http.X-Tenant = "www.ypizza.com";
	return (synth(234));
}

sub vcl_hash {
}

sub vcl_synth {
	set req.url = regsub(req.url, "\?.*$", "");
	if (req.http.X-Tenant) {
		header.append(resp.http.X-Tenant, req.http.X-Tenant);
		header.append(resp.http.X-Tenant, req.http.X-Tenant);
		header.append(resp.http.X-Tenant, req.http.X-Tenant);
	}

	set req.http.X-Url = req.url;
	set resp.http.X-Url = req.http.X-Url;
	set resp.http.X-NotFound = req.http.X-NotFound;
	unset req.http.X-NotFound;
	set resp.http.X-NotFound = regsub(req.http.X-NotFound, "Hello ", "");
	set resp.http.X-NotFound = "1234";

	std.log("Hello VCL World!");
}
