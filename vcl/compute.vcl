vcl 4.1;
import compute;
backend default none;

sub vcl_init {
	# Download and activate a Varnish-provided library of compute programs.
	# A full list of programs and how they can be used would be on the docs site.
	compute.library("https://filebin.varnish-software.com/fh2qb14blnch6r3e/compute.json");
}

sub vcl_backend_fetch {
	if (bereq.url == "/") {
		# Transform a JPEG asset to AVIF, cache and deliver it. cURL can fetch using TLS and HTTP/2.
		set bereq.backend = compute.backend("avif", "https://filebin.varnish-software.com/fh2qb14blnch6r3e/rose.jpg");
	}
	if (bereq.url == "/gzip") {
		# Decompress a zlib-compressed asset
		set bereq.backend = compute.backend("inflate", "https://filebin.varnish-software.com/fh2qb14blnch6r3e/waterfall.png");
	}
	if (bereq.url == "/zstd") {
		# Decompress a zstd-compressed asset into a PNG (without content-type)
		set bereq.backend = compute.backend("zstd", "https://filebin.varnish-software.com/fh2qb14blnch6r3e/waterfall_zstd.png",
			"inline; filename=waterfall.png");
	}
}
