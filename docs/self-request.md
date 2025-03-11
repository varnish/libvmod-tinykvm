# Self Requests

In this documentation you will see a lot of references to Varnish self-requests.

## User-facing explanation

A self-request URL is path-only, like `/path/to/image`. When a TinyKVM program tries to fetch content using a self-request URL, that fetch will be directed directly to Varnish.

When Varnish makes a request to itself it will go through the entire VCL logic again, logging everything and potentially fetching and caching the content. This makes the fetch observable, and we can configure it like normal content.

## Enabling fast self-requests

```sh
varnishd -a /tmp/tinykvm.sock
```
Begin by adding a Unix Domain Socket endpoint to Varnish.

```vcl
tinykvm.init_self_requests("/tmp/tinykvm.sock/");
```
Then configure the Compute VMOD to use this end-point for self-requests. This end-point is very performant and uses Varnish's excellent machinery by running the request through the usual VCL logic, giving the request observability.

_Note that the feature as currently implemented may be replaced by an internal implementation of self-requests in the future._

## High concurrency using self-requests

The most efficient way to use a self-request is to start with one. If you specify `fetch` as the first program in the chain, then the Compute VMOD will not reserve and enter a program, and instead directly fetch the content. This avoids taking up virtual machine resources while the fetch is on-going, allowing many outstanding fetches to happen simultaneously, exactly like normal Varnish operation.

Using this feature it is possible to have a very low number of concurrency in a program, and still service a very high number of slow simultaneous connections.

## Technical explanation

Any URI that is only a path will be assumed to be a self-request from the KVM system API. This means that if I were to use the `sys_fetch` system call to fetch `/my_asset`, Varnish will make a request to itself.

VCL logic will be executed for `req.url == /my_asset` and the source will be `127.0.0.1:6081` (for Unix socket self-requests).

Self-requests currently have to be configured in order to reach Varnish in an efficient manner. This typically means opening up a Unix socket listener on Varnish and then pointing self-requests to that. UDS transmissions are unbuffered and skips basically everything that TCP has to do. Additionally, when connecting with UDS, the operation is reduced to memory operations, directly queuing on the acceptor. There is also no `TIME_WAIT` problems on UDS, making stuck open file descriptors a non-issue here. Benchmarks show that the overhead of making self-requests using UDS is ~100 microseconds for a 28KB cache hit.

## Loop detection

There is currently _no loop detection_ with self-requests, but there is a maximum amount of concurrent self-requests setting that will stop deep recursion. Loops have been triggered many times accidentally during development, and it does eventually unravel.

This setting is configurable.

## Benchmarks

If we benchmark the same program with and without a self-request we get these results, on my machine.

The first benchmark directly processes an in-memory image:

```
$ ./wrk -t1 -c1 -d30s http://127.0.0.1:8080/avif/bench
Running 30s test @ http://127.0.0.1:8080/avif/bench
  1 threads and 1 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     7.25ms  186.01us  12.62ms   90.79%
    Req/Sec   138.51      4.36   141.00     80.33%
  4139 requests in 30.01s, 41.68MB read
Requests/sec:    137.91
Transfer/sec:      1.39MB
```

The second benchmark retrieves a cached asset using a self-request:
```
$ ./wrk -t1 -c1 -d30s http://127.0.0.1:8080/avif
Running 30s test @ http://127.0.0.1:8080/avif
  1 threads and 1 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     7.36ms  153.98us   8.44ms   63.86%
    Req/Sec   136.35      4.88   141.00     59.67%
  4079 requests in 30.03s, 41.07MB read
Requests/sec:    135.84
Transfer/sec:      1.37MB
```

The difference between not fetching at all and fetching from cache is around 100 microseconds. Thus we can say that self-requests introduce a 100 microsecond delay if the asset is already in the cache.
