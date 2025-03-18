# Processing data

## Processing example

Processing data with the TinyKVM VMOD is fairly straight-forward. The process is planned in `vcl_backend_fetch`, executed _in between_ the functions, and the result is available in `vcl_backend_response`.

For example, if we want to produce an on-demand thumbnail of an image, we can use the `thumbnails` program:

```vcl
set bereq.backend = tinykvm.program("thumbnails",
	"/my_asset",
	"""{
		"width": 128
	}""");
```

The `thumbnails` program expects an URL as argument and a width or height as JSON configuration in the second argument. If the URL has no protocol, it is a local asset, and will trigger an efficient self-request to Varnish.

The resulting asset will be downsized to the size from the query parameter, and then returned to back Varnish. From there it is just like any other backend response.

In other words, it is like fetching data from a backend, except we can do some processing directly and efficiently in Varnish.

## Chaining programs

Most compute programs support at least two modes of operation: GET and POST. Using this, we can make the first program use GET, then POST data from one program to the next until the last in the chain which then returns it to Varnish as the final result.

Using this method we can have the whole chaining process automated, by letting the TinyKVM VMOD handle the transfer of the data between the programs, formalizing the method and optimizing for it.

Imagine something like this:
1. Client performs `GET /url1` on Varnish instance
	- Not cached, go to `vcl_backend_fetch`.
2. Invoke two Compute programs
    - The first program in the chain retrieves content from Varnish by making a self-request. A self-request is a cheap way to fetch assets by going through VCL all over again, in order to benefit from caching, logging, etc.
	- The second program in the chain processes the data in some way. For example by compressing it and adding a Content-Encoding.
3. Deliver as normal backend response in Varnish
	- The second program is the last in the chain, and the data returned by the program is delivered straight into Varnish storage, where it can be cached.

## Optimized example

The most optimized processing chains start with a fetch. If fetch URI is a local asset (eg. /path), no program will be reserved and instead the asset is directly fetched. This is nice because even if the fetch takes a long time, as Varnish might need to fetch it from origin, it won't hold up any VM resources.

```vcl
tinykvm.chain("fetch", "/my_asset");
tinykvm.chain("thumbnails", "",
	"""{
		"width": 256
	}""");
set bereq.backend = tinykvm.program("avif");
```

In the above example we fetch `/my_asset` from Varnish, which can trigger an origin fetch, and then the result of that is forwarded to `thumbnails`. Because `fetch` is a special program, it will never hold up any programs. The fetch is entirely in Varnish.

If the fetch operation succeeds, `thumbnails` will produce a thumbnail from the image it received. The smaller image is delivered as a 200 response from the `thumbnails` program. If `thumbnails` failed it would have been possible for it to just forward the original asset. Perhaps a setting in the JSON?

Because `thumbnails` produced a succesful response, it then proceeds to the next program in the chain. In the last and final program in the chain, we transform the thumbnail image with the `avif` program, which transcodes it from JPEG to `Content-Type: image/avif`.

This chain of programs is very efficient and we can expect this to perform very well in production because it's direct and to the point. Each step does one thing and does it well.
