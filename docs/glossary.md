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

* `start`

When enabled, immediately start the program regardless of other initialization settings.

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

* `limit_workmem_after_req`

Request handling operates with 2MB-sized memory banks that are kept after request handling finishes. We can force-free memory back to the system by lowering the number of remaining memory banks. When doing light-weight resets, the limit is used to conditionally perform a full reset in order to keep working memory below a limit. This limit is only active when non-zero and lower than `max_request_memory`.

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

This option allows splitting hugepages during request handling into 4K leaf pages. It adds a level of page-walking needed to access pages, but will keep working memory slightly lower. Usually beneficial.

Default: Enabled

* `ephemeral`

An ephemeral VM is reset after each request concludes, regardless of reason. Ephemeral VMs are reset back to their initialized state, erasing all changes that happened during the request. A non-ephemeral VM will not be reset, which increases performance but without the security and stability guarantees of ephemeralness. If a non-ephemeral VM crashes, runs out of memory or times out, it will be reset. This can also be referred to as _per-request isolation_. If a working memory limit is set and it has been exceeded after the request concludes, a full reset will be executed which reduces memory down to the limit. The freed memory will be given back to the system.

Default: Enabled

* `ephemeral_keep_working_memory`

Keep working memory when resetting the VM after a request completes. This is slower than the regular reset mechanism for _small programs_, however for bigger programs it scales much better. Enabling this also enables `ephemeral`. If a working memory limit is set and it has been exceeded after the request concludes, a full reset will be executed which reduces memory down to the limit. The freed memory will be given back to the system.

Default: Disabled

* `control_ephemeral`

When enabled, a program may self-determine if ephemeral is enabled or not after initialization. The program changes this setting using `sys_make_ephemeral(bool)` before initialization concludes by waiting for requests.

Default: Disabled

* `allow_debug`

Allow remotely debugging requests with GDB. The request to be debugged has to cause a breakpoint. In the C API this is done with `sys_breakpoint()`. The GDB instance must load the program using `file myprogram` before it can remotely connect using `target remote :2159`.

Default: Disabled

* `remote_debug_on_exception`

Start a remote debugging session with GDB as soon as a request VM has an exception thrown. This allows inspecting hard-to-trigger bugs. The GDB instance must load the program using `file myprogram` before it can remotely connect using `target remote :2159`. The session has a 5-minute timeout, after which it is disconnected.

Default: Disabled

* `main_arguments`

An array of arguments that will get passed to the main() function during startup.

```json
	"main_arguments": [
		"--jitless"
	]
```

* `environment`

An array of environment variables appended to the programs environ, accessible with getenv().

```json
	"environment": [
		"VERBOSE=1"
	]
```

The guest program has 5 environment variables set at start:

	- KVM_NAME=program_name
	- KVM_GROUP=group_name
	- KVM_TYPE=storage or request
	- KVM_STATE=state.file
	- KVM_DEBUG=0 or 1

These variables are always present.

* `allowed_paths`

An array of paths specifying each individual file that a program is allowed to read from. Programs are allowed to list directiories and read files, in read-only mode.

```json
	"allowed_paths": [
		"/usr/lib/x86_64-linux-gnu/espeak-ng-data",
		"/usr/local/share/espeak-ng-data"
	]
```

Example paths for the espeak-ng text-to-speech generator.

```json
	"allowed_paths": [
		"/dev/urandom",
		"/lib/x86_64-linux-gnu/libz.so.1",
		"$/home/deno",
		{
			"virtual": "/main.ts",
			"real": "/home/deno/main.ts"
		}, {
			"real": "/home/gonzo/.cache/deno/remote",
			"writable": true,
			"prefix": true
		}, {
			"virtual": "/",
			"real": "/home/deno"
		}, {
			"virtual": "/proc/self/exe",
			"real": "/home/user/myprogram.elf",
			"symlink": true
		}
	],
```

Example paths for a Deno program. It will rewrite `/main.ts` to `/home/deno/main.ts` hiding the real path. Prefixes are paths that start with the given string, and any string that starts with the prefix, but is longer will still match. Writable prefixes are dangerous. Symlinks are resolved by matching against the virtual path and returning the real path as the answer.

* `current_working_directory`

Sets the current working directory used by `getcwd` and other system calls in guest programs. The path must exist and be accessible such that AT_CWDFD can be used as relative to it.

* `mmap_backed_files`

Whenever the guest maps a file, map the file into VM guest memory without copying. When disabled, the file will be copied into guest memory, increasing RSS, but avoiding extra mappings.

Default: Enabled

* `verbose`

Enable verbose output from program loading, as well as from certain system calls. For example, inaccessible file paths will be printed to console.

* `verbose_syscalls`

Enable verbose output from all system calls. This creates quite a bit of logging and is only useful when debugging complex issues in programs. Only enabled for the main VM, and disabled on request VMs.

* `verbose_pagetables`

Print the main VMs pagetable state after initialization.

* `remapping`

Allocate memory from the guests heap, unmap the area and move it to the desired location. This feature allows setting up high-memory areas without needing a large address space, which has been shown to increase performance. Remappings are named objects with an array of either `["0xADDRESS", size_megabytes]` or `["0xADDRESS", "0xADDRESS"]`. Example:

```json
	"remapping": {
		"caged_heap": ["0x1000000000", 256],
		"another_heap": ["0xC00000000", 64]
	},
```
V8 has a caged_heap feature which requires a certain bit set in order to work. We don't want to create a 64+GB address space, so instead we allocate a tiny area at the desired location, and we named it `caged_heap`.

* `blackout_area`

Unmap the specified area with similar rules to `remapping`. Example:

```json
	"blackout_area": {
		"unmap_before_caged_heap": ["0x200000000", "0x1000000000"],
	},
```
This will unmap 8-64GB memory areas, making them unavailable. Blacking out unused areas can improve performance and reset times, but must be done carefully. If the guest tries to use the area it will cause a CPU exception. It's usually better to have a small address space with remappings.

* `warmup`

Send mock requests to the VM before forking it, allowing run-times to warm up.

```json
	"warmup": {
		"num_requests": 50,
		"url": "/warmup",
		"headers": [
			"Host: localhost",
			"X-Request-Id: warmup"
		]
	}
```
