# Storage

## About storage

Storage is a big topic, so let's start with some basics:

- Storage is a sandboxed computer program.
- It is long-lived (never gets wiped).
- Completely separate from requests (with gated access), running on its own.
- Has access to persistence and live-update mechanisms.
- Most compute programs _will not_ use it.
- Can remember things from one request to the next.
- Can perform periodic tasks.

Let's think of storage as a separate VM that each program may have. Sometimes the storage VM mirrors the request VM, in that they share the same source program at startup, but then they will diverge over time. Request VMs are ephemeral and will be completely wiped after each request, while storage will go on. Other times the storage program is a completely different one, and is quite inscrutable from the request VMs point of view.

Because storage is a long-lived guest program, it must be written defensively. Crashes will not be forgiving if requests rely on storage for accurate information. As always, crashes cannot harm the Varnish server, but calling into a broken program is undefined.


## Storage calls

A storage call is a system call that a request can invoke in order to access storage. It passes a function pointer as argument, as well as a vector of buffers as input to storage, and one buffer as output from storage.

```c
typedef void (*storage_func) (size_t n, struct virtbuffer[n], size_t res);

extern long
storage_callv(storage_func, size_t n, const struct virtbuffer[n], void* dst, size_t);
```

Only one request can access storage at a time. This serializing access ensures the integrity of storage.

Example:


## Storage tasks

A storage task is something that can be scheduled to happen eventually. It can be scheduled with periodic timing, one-shot or immediately queued up.

All storage tasks use the same serializing queue that other storage calls use, meaning that there is never any concurrency issues.

Example:



## Persistence

When unloading a program and loading a new one, it is possible for the storage program to serialize its most important bits and deserialize them in the new one, effectively becoming a live-update.

A program can also write its state to a file. Every storage VM has a single file it can write to simply called `state` inside the VM.

## Live-update sequence

Let's start with an example:

```c
static char  *cont = NULL;
static size_t cont_len = 0;

static void on_live_update()
{
	/* Serialize data we want to take with us to the next program. */
	storage_return(cont, strlen(cont));
}
static void on_resume_update(size_t len)
{
	/* Make room for data we want to receive from the old program. */
	free(cont);
	cont = malloc(len);
	/* Live-update mechanism will fill 'cont' with data. */
	storage_return(cont, len);
	/* Here we can deserialize 'cont' into something more useful. */
}

int main(int argc, char **argv)
{
	...
	set_on_live_update(on_live_update);
	set_on_live_restore(on_resume_update);

	wait_for_requests();
}
```

This simplified program can serialize a buffer and pass it from an old to-be-replaced program, and forward it to the next program once an update happens. Updates typically happen when a program is unloaded and there is a new program available, while the live-update callbacks are set.


## Secrets

_WARNING: Currently, the storage VMs memory is allocated in the Varnish worker address space, however there are ideas to improve the situation._

See: AMD SEV.

## Performance

While accessing storage is serialized to one by one, it's quite performant. Storage does not need to be wiped after each request and will have its caches working on each access, making it quite fast.
