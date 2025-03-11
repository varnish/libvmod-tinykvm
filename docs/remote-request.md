# Remote requests

Occasionally you will want to make a fetch to a remote asset, without going through Varnish. This is supported, and there is also ActiveDNS suport.

Note: ActiveDNS support is only enabled in Varnish Enterprise.

```vcl
sub vcl_init {
	new filebin = activedns.dns_group();
	filebin.set_host("filebin.varnish-software.com");

	compute.library("...");
}

sub vcl_backend_fetch {
	set bereq.backend = compute.program("zstd",
		"https://${filebin}/.../image-asset.png",
		"""{
			"action": "compress",
			"level": 6,
			"headers": ["Host: filebin.varnish-software.com"]
		}""");
}
```

In this example we compress an asset with Zstandard. The asset comes from a filebin, and we use an ActiveDNS tag (`${filebin}`) for DNS resolution. Because the DNS resolution resolves to an IP address, we will need to supply a Host header to the request. It is up to each program what configuration they support.

This method, despite working, suffers from lack of observability. We recommend going through Varnish using self-requests for the logging and extra configurability. Sometimes it's also helpful to cache the asset before and after processing.
