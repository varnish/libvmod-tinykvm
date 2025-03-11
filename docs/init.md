# Initialization

Initializing the TinyKVM VMOD always starts by fetching a library from either a remote URL or a local file (using `file:///path/to/library.json`).

All initialization and configuration must happen in `vcl_init`.

## Libraries

```vcl
import tinykvm;

sub vcl_init {
	tinykvm.library("https://path/to/compute/library.json");
}
```
This will download and activate a Varnish-provided library of compute programs. Also known as a _compute library_. A full list of programs and how they can be used would be on the docs site.

The JSON file that is fetched contains definitions about existing programs and how/where to get them. Each program has an associated default configuration, including a group it belongs to. More on that later.

Note that if the library JSON is local on the machine, it must be made accessible to the varnish user.

Varnish Software maintains a high quality program library. It is [perhaps documented here](https://docs.varnish-software.com).

## Delayed start

When using a compute program, that program is not yet started until the first time someone attempts to use it. If you want to start a program as part of `vcl_init`, you can use `tinykvm.start()`:

```vcl
sub vcl_init {
	tinykvm.library("...");
	tinykvm.start("my_program");
}
```
This will start `my_program` during initialization. The start is asynchronous and will not delay the startup of Varnish.

All programs cannot be started by default because they consume resources such as memory mappings, file descriptors and create threads.

## Basic configuration

It is possible to tweak the configuration of a program in `vcl_init` by overriding its initial configuration. Typically we want to tweak concurrency level, timeouts and occasionally memory limits.

### Overriding concurrency level

```vcl
tinykvm.configure("my_program",
	"""{
		"concurrency": 4
	}""");
```
This is the number of VMs to initialize, where each one will be able to handle a request. With a concurrency level of 4 it is possible to handle 4 requests at a time, concurrently. A large amount of VMs can be anything from hundreds to thousands depending on how many other programs are also being used.

### Enabling fast self-requests

```sh
varnishd -a /tmp/tinykvm.sock
```
Begin by adding a Unix Domain Socket endpoint to Varnish.

```vcl
tinykvm.init_self_requests("/tmp/tinykvm.sock/");
```
Then configure the TinyKVM VMOD to use this end-point for self-requests.

See the [self-request](/self-request) page for more information. Note that this feature may be replaced completely by an internal implementation of self-requests in the future.

### Overriding memory limits

```vcl
tinykvm.configure("my_program",
	"""{
		"max_memory": 512,
		"max_request_memory": 128
	}""");
```
This is the memory in MBs to give the main VM and the request VMs. The main VM initializes the main program itself, and it's safe to give it a lot of memory to work with, however it will rarely use all of it. Request VM memory is what each request will have to work with, typically just enough to process something.

### Overriding boot and request timeouts

```vcl
tinykvm.configure("my_program",
	"""{
		"max_boot_time": 16.0,
		"max_request_time": 120.0
	}""");
```
The startup of a program is called boot, and it is usually very short. If a large fetch is involved it can take longer, and so it may need to be increased to account for that. The request time is the maximum time a single request may take, and it can be everything from 5 seconds to several minutes depending on the workload.

### Giving access to local files

```vcl
tinykvm.configure("my_program",
	"""{
		"allowed_paths": [
			"/tmp/some.file",
			"/some/other.file"
		],
	}""");
```
A program has no local filesystem access by default. However, it can be given access to individual (single) files, by listing each file individually in the allowed_paths array.

### Enabling hugepages

```vcl
tinykvm.configure("my_program",
	"""{
		"hugepages": true,
		"request_hugepages": true
	}""");
```
VMs typically benefit immensely from hugepages, and it often accelerates the performance of the program beyond native performance. Programs will not respond equally to hugepages, some benefitting more than others.

Hugepages need to be allocated by the kernel, by writing to nr_hugepages in vfs for 2MB pages (amd64).

## Reloading programs

It is possible to unload a program while the server is running. After the program is unloaded, the next request that comes in to the program will load it again. This feature can be used to update a program without reloading VCL.

```vcl
sub vcl_recv {
	if (req.url == "/invalidate") {
		tinykvm.invalidate_programs("my_program");
		return (synth(200));
	}
}
```
The parameter is a regex pattern, and for example an empty string can be used to unload all programs. While the new program is being fetched and initialized, new requests will be queued up.

## Guidelines

Typically Varnish Software will maintain good defaults for each program, and users of the program will most likely only have to tweak the concurrency level to deal with their individual request rates.

Here are some tips for how to best tweak compute programs:

- Self-requests have very little overhead. Use them liberally.

- Keep concurrency low, while still maintaining a decent request rate on that URL. Concurrency here is not like when handling regular requests. This is concurrency for CPU-heavy workloads, and it needs to be conservatively provisioned.

- It's OK to give out lots of request memory, as long as it gets reclaimed after. See `req_mem_limit_after_reset`.

