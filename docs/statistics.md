# Explanations

You can view a snapshot of statistics taken from a [live instance here](public/stats.json).

## Stat counters

All global TinyKVM and KVM stat counters start with VMOD_KVM, and are mainly used for monitoring purposes. They are statistics dedicated to detecting live issues. All the following stat counters use atomic ops.

> VMOD_KVM.program_notfound

Total number of programs that were not able to be loaded on demand. These programs have failed initialization or the program archive could not be fetched.

> VMOD_KVM.program_exception

Total number of programs raising an exception and getting cancelled.

> VMOD_KVM.program_timeout

Total number of programs running out of allotted processing time and getting cancelled.

> VMOD_KVM.program_status_4xx

Total number of HTTP 400-499 status errors as reported by programs.

> VMOD_KVM.program_status_5xx

Total number of HTTP 500-599 status errors as reported by programs.

> VMOD_KVM.cpu_time

Over-estimated total CPU-time spent by all programs. Does not subtract scheduling down-time. Used to monitor for anomalies and big-picture issues where something is suddenly spending a lot CPU-time.

> VMOD_KVM.fetch_time

Total time spent self-requesting assets from Varnish for programs.

> VMOD_KVM.self_requests

Total number of self-requests between all programs.

> VMOD_KVM.self_requests_failed

Total number of failed self-requests between all programs.

## JSON statistics

Each program matching the pattern from the `tinykvm.stats()` VCL call will have statistics appended to the JSON document.

The general layout is that each program is a unique object, where each program can store each of their respective request and storage VMs as sub-objects:

```json
{
	"avif": {
		"program": {
			"live_update_transfer_bytes": 0,
			"live_updates": 0,
			"reservation_time": 0.08435964584350586,
			"reservation_timeouts": 0
		},
		"request": {
			"...": "..."
		}
	}
}
```

Some programs have a `storage` sub-object.

The JSON information may seem a bit over-engineered, however the reason behind splitting the stats by request machines is to avoid atomic operations. Atomics are notorious on NUMA-systems and causes stalls even in normal systems if they are littered everywhere.

> Atomic instructions bypass the store buffer or at least they act as if they do - they likely actually use the store buffer, but they flush it and the instruction pipeline before the load and wait for it to drain after, and have a lock on the cacheline that they take as part of the load, and release as part of the store - all to make sure that the cacheline doesn't go away in between and that nobody else can see the store buffer contents while this is going on. - Linus Torvalds

## Program object

- `live_updates`
	- The number of times this program has been live-updated.
- `live_update_transfer_bytes`
	- The number of bytes transferred between the old and the new program when live-updated.
- `reservation_time`
	- Time all requests have spent waiting for exlusive access to a request VM.
- `reservation_timeouts`
	- The number of times requests have failed due to waiting too long for a request VM.

## Request object

The request object has an array of machines (one for each request concurrency level), and an object that contains the cumulative totals of all the request machines.

- `exception_cpu_time`
	- Time spent
- `exceptions`
	- Time spent
- `input_bytes`
	- Bytes transferred in for processing.
- `output_bytes`
	- Bytes produced as final output from processing.
- `invocations`
	- Number of times a machine has been used for processing.
- `request_cpu_time`
	- CPU-time spent processing.
- `reservation_time`
	- Time spent getting exclusive access to this machine.
- `resets`
	- Number of times a machine has been completely reset to its initial state, wiping memory.
- `status_2xx`
- `status_3xx`
- `status_4xx`
- `status_5xx`
	- Status codes reported after processing.
- `tasks_queued`
	- Number of tasks currently queued up. Only useful on storage machine.
- `timeouts`
	- Number of times processing has been interrupted due to taking too long.

## Storage object

- `tasks_inschedule`
	- Active tasks currently scheduled to at some point execute in storage.
- `tasks_queued`
	- Number of tasks currently queued up waiting for storage access. Can be contentious.
