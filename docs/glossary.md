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

The maximum number of seconds of access to the shared storage a program is allowed to take.

Granularity: seconds

* `max_memory`

The maximum memory usage allowed during both request initialization and storage initialization.

Granularity: 2 megabytes

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

* `address_space`

The maximum accessible address, with a maximum limit of 512GB. This value will automatically be adjusted to accommodate `max_memory`.

Granularity: 2 megabytes

* `hugepages`

Enable hugepages for the entire address space during initialization. Not recommended. Do *NOT* enable this unless you have enough hugepages allocated beforehand.

Default: Disabled

* `hugepage_arena_size`

Enable hugepages for main memory for the given number of megabytes, after which use normal pages. Setting this to zero will disable hugepages, and setting it to 2 MB or higher will enable them up to that point. Do *NOT* enable this unless you have enough hugepages on the host system allocated beforehand.

Granularity: 2 MB

* `request_hugepage_arena_size`

Enable hugepages for request handling (memory banks) up to the given number of megabytes, after which use normal pages. This can result in a modest performance increase (above native performance). Do *NOT* enable this unless you have enough hugepages on the host system allocated beforehand. This setting scales with the concurrency level of the program, eg. concurrency=32 with request_hugepage_arena_size=16 would require 256x 2MB hugepages.

Granularity: 2 MB

* `split_hugepages`

With very high concurrency, constraining memory as much as possible may be desirable. This option allows splitting hugepages during request handling into 4K leaf pages. It adds a level of page-walking needed to access pages, but will keep working memory slightly lower.

Default: Disabled

* `ephemeral`

An ephemeral VM is reset after each request concludes, regardless of reason. Ephemeral VMs are reset back to their initialized state, erasing all changes that happened during the request. A non-ephemeral VM will not be reset, which increases performance but without the security and stability guarantees of ephemeralness. If a non-ephemeral VM crashes, runs out of memory or times out, it will be reset.

Default: Enabled

* `control_ephemeral`

When enabled, a program may self-determine if ephemeral is enabled or not after initialization. The program changes this setting using `sys_make_ephemeral(bool)` before initialization concludes by waiting for requests.

* `allow_debug`

Allow remotely debugging requests with GDB. The request to be debugged has to cause a breakpoint. In the C API this is done with `sys_breakpoint()`. The GDB instance must load the program using `file myprogram` before it can remotely connect using `target remote :2159`.


* `remote_debug_on_exception`

Start a remote debugging session with GDB as soon as a request VM has an exception thrown. This allows inspecting hard-to-trigger bugs. The GDB instance must load the program using `file myprogram` before it can remotely connect using `target remote :2159`. The session has a 5-minute timeout, after which it is disconnected.


* `main_arguments`

An array of arguments that will get passed to the main() function during startup.

```json
	"main_arguments": [
		"--jitless"
	]
```

The guest program has 3 mandatory arguments: The program, a single writable state file and an argument indicating whether or not the instance is a storage or request VM. The `main_arguments` array is added after these arguments. Using the JSON above as example, one might see: `my_program state request --jitless`.


* `environment`

An array of environment variables appended to the programs environ, accessible with getenv().

```json
	"environment": [
		"VERBOSE=1"
	]
```

* `allowed_paths`

An array of paths specifying each individual file that a program is allowed to read from. Programs are allowed to list directiories and read files, in read-only mode.

```json
	"allowed_paths": [
		"/usr/lib/x86_64-linux-gnu/espeak-ng-data",
		"/usr/local/share/espeak-ng-data"
	]
```

Example paths for the espeak-ng text-to-speech generator.

* `verbose`

Enable verbose output from program loading, as well as from certain system calls. For example, inaccessible file paths will be printed to console.

* `verbose_pagetables`

Print the main VMs pagetable state after initialization.
