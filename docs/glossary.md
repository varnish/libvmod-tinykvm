# JSON Glossary

The Compute VMOD uses JSON to configure individual programs and group settings. The JSON supports comments, but *NOT* trailing commas.

## A compute group

```json
	"compute": {
		"concurrency": 4,
		"max_boot_time": 8.0,
		"max_request_time": 6.0,
		"max_memory": 64,
		"max_request_memory": 48,
		"req_mem_limit_after_reset": 16 /* Mbytes */
	},
```

A group is a bunch of settings grouped together, and can be referred to by any program.

Note that overriding the group during `vcl_init` has no effect as the groups settings have already been applied. In other words _this does NOT work_:

```vcl
compute.configure("my_program",
	"""{
		"group": "new_group" // Cannot be changed in vcl_init
	}""");
```

However, changing any other setting works:
```vcl
compute.configure("my_program",
	"""{
		"concurrency": 4,
		"max_request_time": 16.0
	}""");
```

## A compute program

```json
	"inflate": {
		"group": "compute",
		"uri": "https://some.where/kvmprograms/inflate",
		"filename": "/tmp/compute_inflate"
	},
```

This program is part of the group `compute`. It can be retrieved from the given `uri`, and we cache it locally at the given `filename`. That way, when we start the program again later, we can see that the fetch returns 304, and we already have it.

## A non-trivial example

```json
{
	"compute": {
		"concurrency": 8,
		"max_boot_time": 8.0,
		"max_request_time": 6.0,
		"max_memory": 64,
		"max_request_memory": 48,
		"req_mem_limit_after_reset": 16 /* Mbytes */
	},
	"demo": {
		"group": "compute",
		"uri": "https://some.where/kvmprograms/demo",
		"filename": "/tmp/compute_demo",
		"concurrency": 1
	}
}
```

With this configuration we can fetch the `demo` program on-demand, and cache it at `/tmp/compute_demo`.

Store the JSON in a local file and then load it with `compute.library("file:///path/to/compute.json")`. The `demo` program can then be referred to in the VCL.

## Configuration settings

* `group`

Specify a group from which to get initial settings from.

* `uri`

Specify a URI from which to get the latest version of a program from.

* `filename`

Specify a location where a program can be found, or stored if fetched.

* `concurrency`

Specify the number of concurrent/simultaneous requests available for processing. A separate ultra-thin request-handling VM is forked from the main VM for each level of concurrency.

There is a queue with a 60 second timeout for extranous requests.

* `storage`

Specify whether or not this program has access to shared mutable storage. Programs that need to remember things across requests will need storage. Programs designed to use storage need to have this enabled in order to function correctly.

* `max_boot_time`

The time it takes before giving up when starting a program. If the program takes too long to start it may have gotten stuck on something, like a loop, or a request that doesn't complete.

Granularity: seconds

* `max_request_time`

The maximum number of seconds request handling for a program is allowed to take.

Granularity: seconds

* `max_storage_time`

The maximum number of seconds access to the shared storage for a program is allowed to take.

Granularity: seconds

* `max_memory`

The maximum memory usage allowed during both request initialization and storage initialization.

Granularity: megabytes

* `max_request_memory`

The maximum memory usage allowed during request handling. Request handling is layered on top of the pre-initialized programs, and will have access to all the memory from initialization, but can make changes on top of that. These changes evaporate after the request handling finishes.

You can think of it like this: If you have a variable that you set at the start:
```C
static int my_value;
int main()
{
	my_value = 4;
}
```
Then that variable will always have that value at the start of a request. If you change it, it will be changed for that request only, and the change will evaporate after the request ends.

This limit is measuring the memory it takes to make the layered changes. If you hit this limit the request ends, and will log that it ran out of request working memory.

Granularity: megabytes

* `req_mem_limit_after_reset`

Request handling operates with 2MB-sized memory banks that are kept after request handling finishes. We can force-free memory back to the system by lowering the number of remaining memory banks.

Granularity: 2 megabytes

* `shared_memory`

Allows requests to share memory with each other at the end of the available memory. This memory is not shared with and does not require storage.

Granularity: 2 megabytes

Default: No shared memory

* `hugepages`

Enable hugepages for the entire address space during initialization. Not recommended. Do *NOT* enable this unless you have enough hugepages allocated beforehand.

Default: Disabled

* `request_hugepages`

Enable hugepages for request handling (memory banks). This can result in a modest performance increase (above native performance). Do *NOT* enable this unless you have enough hugepages allocated beforehand.

Default: Disabled

* `environment`

An array of environment variables appended to the programs environ, accessible with getenv().

```json
	"environment": [
		"VERBOSE=1"
	]
```

* `allowed_paths`

An array of paths specifying each individual file that a program is allowed to read from. Programs are only allowed to read files, and only individual files.

```json
	"allowed_paths": [
		"/usr/lib/x86_64-linux-gnu/espeak-ng-data",
		"/usr/local/share/espeak-ng-data"
	]
```

Example paths for the espeak-ng text-to-speech generator.
