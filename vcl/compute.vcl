vcl 4.1;
import compute;
backend default none;

sub vcl_init {
	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	compute.library("https://filebin.varnish-software.com/nsyb0c1pvwa7ecf9/compute.json");
	# Start the AVIF transcoder, but don't delay Varnish startup.
	compute.start("avif");
}

sub vcl_backend_fetch {
	if (bereq.url == "/") {
		# Transform a JPEG asset to AVIF, cache and deliver it. cURL can fetch using TLS and HTTP/2.
		set bereq.backend = compute.backend("avif", "https://filebin.varnish-software.com/nsyb0c1pvwa7ecf9/rose.jpg");
	}
	if (bereq.url == "/gzip") {
		# Decompress a zlib-compressed asset
		set bereq.backend = compute.backend("inflate", "http://httpbin.org/gzip");
	}
	if (bereq.url == "/zstd") {
		# Decompress a zstd-compressed asset into a PNG (without content-type)
		set bereq.backend = compute.backend("zstd", "https://filebin.varnish-software.com/nsyb0c1pvwa7ecf9/waterfall_zstd.png",
			"inline; filename=waterfall.png");
	}
	if (bereq.url == "/ftp") {
		# Fetch something with cURL
		set bereq.backend = compute.backend("fetch", "ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt");
	}
	if (bereq.url == "/http3") {
		# Fetch HTTP/3 page with cURL. AltSvc cache enables future fetches to use HTTP/3.
		set bereq.backend = compute.backend("fetch", "https://quic.rocks:4433/");
	}
}
sub vcl_backend_response {
	if (bereq.url == "/http3") {
		set beresp.uncacheable = true;
	}
}
