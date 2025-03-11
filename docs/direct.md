# Direct invocations

## Invoke program from anywhere

If you want to compute a result inside any function, you can use `tinykvm.to_string()`. The program is always invoked with the GET method, forwarding arguments from VCL. It produces a string result directly:

```vcl
set bereq.http.X-FTP =
	tinykvm.to_string("fetch",
		"ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt");
```
Fetches the whole FTP RFC to a string. Other uses are gathering and analyzing requests. Because the `to_string()` variant doesn't need to reserve a VM until the end of the request, it will be quite fast, and will not require high concurrency in order to operate on normal traffic.

Perhaps in the future there will a `to_blob()` function as well. We'll see.

## Create synthetic response

If you want to compute a synthetic response from a program, you can use `tinykvm.synth()`. The program is always invoked with the GET method, forwarding arguments from VCL. It produces a synthetic response directly:

```vcl
sub vcl_backend_error {
	tinykvm.synth(beresp.status, "fetch", "https://http.cat/" + beresp.status);
	return (deliver);
}
```

Normalize cute cat pictures as HTTP status responses! Note that we can fetch cached images from Varnish here instead of hammering http.cat. The direct URL simplifies the example.


### to_string() Benchmark

Running wrk with 16 threads, *without* collector program:
```sh
$ ./wrk -t8 -c8 -d15s http://127.0.0.1:8080/avif/image
Running 15s test @ http://127.0.0.1:8080/avif/image
  8 threads and 8 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    18.21us    6.05us 784.00us   81.11%
    Req/Sec    53.88k    12.02k   73.05k    49.75%
  6476337 requests in 15.10s, 169.36GB read
Requests/sec: 428900.67
Transfer/sec:     11.22GB
```

Running wrk with 16 threads, *with* collector program:
```sh
$ ./wrk -t8 -c8 -d15s http://127.0.0.1:8080/avif/image
Running 15s test @ http://127.0.0.1:8080/avif/image
  8 threads and 8 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    26.98us    6.00us 545.00us   77.11%
    Req/Sec    36.62k     4.94k   44.48k    63.74%
  4400955 requests in 15.10s, 115.12GB read
Requests/sec: 291466.50
Transfer/sec:      7.62GB
```

The URL collector adds only 9us overhead to each request, in a benchmark that saturates my CPU.


### synth() Benchmark

Producing a synthetic response instead of backend responses is also possible. They have a bit of extra overhead in the Varnish Cache itself, but that is something that can be solved with time.

Running wrk with 16 threads, producing a counted hello world with the `counter` program:
```sh
$ ./wrk -c16 -t16 -d10s http://127.0.0.1:8080/scounter
Running 10s test @ http://127.0.0.1:8080/scounter
  16 threads and 16 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    34.70us    7.03us 488.00us   72.43%
    Req/Sec    28.51k   667.75    29.83k    78.40%
  4584125 requests in 10.10s, 0.88GB read
Requests/sec: 453899.56
Transfer/sec:     88.74MB

$ curl http://127.0.0.1:8080/scounter
Hello 9065374 World!
$ curl http://127.0.0.1:8080/scounter
Hello 9065375 World!
```

That's not too bad! Very close to a cache hit. Perhaps we can be more equal to a cache hit if we can work on the internal overheads in Varnish?
