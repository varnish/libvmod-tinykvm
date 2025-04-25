# Remote requests

Occasionally you will want to make a fetch to a remote asset, without going through Varnish.

```vcl
sub vcl_init {
	compute.library("...");
}

sub vcl_backend_fetch {
	set bereq.backend = compute.program("zstd",
		"https://filebin.varnish-software.com/.../image-asset.png",
		"""{
			"action": "compress",
			"level": 6
		}""");
}
```

In this example we compress an asset with Zstandard.

This method, despite working, lacks the same level of observability that varnishlog provides. You can see the transfer in the logs, but not like a full request in Varnish. However, you can go through Varnish using a self-request for the logging and extra configurability. Sometimes it's also helpful to cache the asset before processing.
