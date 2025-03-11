# Monitoring

Monitoring the health of compute programs is an essential part of running in production. The TinyKVM VMOD publishes global counters for the overall health of the compute platform, but also individual fine-grained statistics published as JSON accessible through VCL.

## Global statistics

All compute programs share the global CPU-time and fetch-time counters that counts seconds spent processing and fetching data. The counters are called `VMOD_KVM.cpu_time` and `VMOD_KVM.fetch_time` respectively.

Napkin math says that if we benchmark an AVIF transcoder with 8 cores for 15 seconds we should be spending (a little bit less than) 8*15 = 120 seconds fetching and processing data. So a bit less than 2 minutes.

### Verification

```sh
$ ./wrk -t8 -c8 -d15s http://127.0.0.1:8080/avif
Running 15s test @ http://127.0.0.1:8080/avif
  8 threads and 8 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    26.91ms   97.78ms 781.85ms   95.80%
    Req/Sec   128.63     13.18   151.00     94.49%
  14683 requests in 15.01s, 147.88MB read
Requests/sec:    978.14
Transfer/sec:      9.85MB
```

Varnish stat counter:
```
VMOD_KVM.cpu_time                                             0+00:01:51
VMOD_KVM.fetch_time                                           0+00:00:07
VMOD_KVM.self_requests                                             14689
```

This is very close to the estimate. The closer the CPU-time + fetch-time is to 2:00 minutes, the less time was spent in Varnish and more time was spent transcoding.

Using this we can check if something is very wrong. If one of the programs are using more CPU-time than expected we can investigate deeper by checking each individual program.

## Detailed statistics

The VCL function `compute.stats()` produces JSON output based on a regex that matches one or more programs. For each matching program the JSON output will append an object with detailed information about the CPU-time, usage count, errors and so on.

NOTE: These counts come directly from the programs, and there may be other steps (including VCL) that can modify eg. HTTP status codes. So, if there is a mismatch between 5xx codes and the programs are reporting 4xx, there may be a failing request or VCL that modifies/sanitizes failures.

### JSON output from benchmarking

[Click here to see JSON output after running a few AVIF benchmarks](stats.json)

Collapsing all and expanding the totals for `avif` we find:
```json
{
	"totals": {
		"exception_cpu_time": 0.0,
		"exceptions": 0,
		"input_bytes": 1156489931,
		"invocations": 41572,
		"output_bytes": 427363875,
		"request_cpu_time": 450.500058716,
		"resets": 0,
		"status_2xx": 41572,
		"status_3xx": 0,
		"status_4xx": 0,
		"status_5xx": 0,
		"timeouts": 0
	}
}
```

Expanding the totals for the AVIF program, we can see that it spent 450 CPU-seconds transcoding. It never had any exception handling running (0 seconds), and all status codes are 2xx.

The program received 1102 MB in and produced 407 MB output. A 63% reduction in size with the default transcoder settings.

```sh
$ ./wrk -t16 -c16 -d15s http://127.0.0.1:8080/avif
Running 15s test @ http://127.0.0.1:8080/avif
  16 threads and 16 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    10.66ms    1.87ms  23.21ms   78.91%
    Req/Sec    94.08     10.61   121.00     68.88%
  22523 requests in 15.02s, 226.85MB read
Requests/sec:   1500.03
Transfer/sec:     15.11MB
```

One of the benchmarks running was using 16 threads, and involved a self-request to Varnish, fetching a cached image. We were producing 1500 AVIF images/sec!
