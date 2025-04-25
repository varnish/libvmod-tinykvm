# A storage program example

Shared storage is a feature that allows each program to have its own mutable state, accessible from each request. There are various features for asynchronously performing tasks in storage.

Fundamentally, storage is a program, and usually the same program that you handle requests in, except it's a different virtual machine, with its own memory. At the start, both the initial request program and the initial storage program is exactly the same. They will only diverge over time. This allows them to speak to each using constants that you already know at compile time, such as where functions and global variables are.

In order to use shared storage between requests we will need to enable storage.

```json
	"storage_test": {
		"group": "compute",
		"uri": "https://some.where/compute/storage_test",
		"filename": "/tmp/compute_storage_test",
		"storage": true
	},
```
Enabling storage is as simple as setting `storage` to `true`. Instead of one program running through main, now there is two. Third argument to the program will allow you to distinguish between `request` and `storage`.

```C
#include "kvm_api.h"
#include <string.h>
#include <stdio.h>
static int storage_counter = 0;

static void increment_counter(size_t n, struct virtbuffer buf[n], size_t res)
{
	storage_counter ++;

	storage_return(&storage_counter, sizeof(storage_counter));
}

static void
on_get(const char *url, const char *arg)
{
	int counter;
	storage_call(increment_counter, 0, 0, &counter, sizeof(counter));

	http_setf(BERESP, "X-Counter: %d", counter);

	backend_response_str(200, "text/plain", "Hello Compute World!");
}

int main(int argc, char **argv)
{
	/* IS_STORAGE() macro tells us if we are currently in the storage VM */ 
	if (IS_STORAGE()) {
		puts("Hello Storage World!");
		fflush(stdout);
	}

	/* Only allow this function to be called in storage */
	STORAGE_ALLOW(increment_counter);

	set_backend_get(on_get);
	wait_for_requests();
}
```

Here we set the X-Counter header to an atomically incrementing counter each time the program is executed. The only storage function that can be called is `increment_counter`.

## Verification

```
Info: Child () said Loading 'counter' from https://.../counter.tar.xz
Info: Child () said Found 'c/counter' size=902024, used as request program
Info: Child () said >>> [counter] Hello Storage World!
Info: Child () said Program 'counter' is loaded (remote, not cached, vm=8, huge=0/0, ready=1.60ms)
```

Loading the program, and then fetching a few times, we get:
```sh
$ curl -D - http://127.0.0.1:8080/counter
HTTP/1.1 200 OK
X-Counter: 9
Content-Type: text/plain
Content-Length: 20
Last-Modified: Wed, 23 Aug 2023 19:50:30 GMT
Date: Wed, 23 Aug 2023 19:50:30 GMT
X-Varnish: 14
Age: 0
Via: 1.1 varnish (Varnish/6.0)
Accept-Ranges: bytes
Connection: keep-alive

Hello Storage World!
```

We can see that the counter is increasing, and that we have state that is accessible from subsequent requests. Using this we can implement application logic on the edge.

The storage access is serialized: Only one request can access storage at a time. So be aware that it has a cost. Benchmarking the endpoint, we find a lower than normal RPS:

```sh
$ ./wrk -c8 -t8 http://127.0.0.1:8080/counter
Running 10s test @ http://127.0.0.1:8080/counter
  8 threads and 8 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     6.34ms   38.24ms 360.95ms   96.97%
    Req/Sec     9.92k     0.94k   11.69k    98.59%
  771195 requests in 10.10s, 214.86MB read
Requests/sec:  76361.21
Transfer/sec:     21.27MB

$ curl -D - http://127.0.0.1:8080/counter
HTTP/1.1 200 OK
X-Counter: 771197
```

And that the benchmark has indeed triggered the counter many times. :) 

# A shared memory example

We can avoid storage altogether and instead have a shared memory area at the end of the available memory. The JSON option `shared_memory` with a 2MB granularity will set this up automatically:

```json
	"counter": {
		"group": "compute",
		"uri": "https://.../counter.tar.xz",
		"filename": "/tmp/compute_counter",
		"shared_memory": 2
	},
```

With this we can simplify the program to just do an atomic increment instead:

```c
#include "kvm_api.h"
#include <string.h>
#include <stdio.h>
static int *counter;

static void
on_get(const char *url, const char *arg)
{
	const int c = __sync_fetch_and_add(counter, 1);

	char buffer[64];
	const int len =
		snprintf(buffer, sizeof(buffer), "Hello %d World!", c);

	backend_response(200, "text/plain", strlen("text/plain"), buffer, len);
}

int main(int argc, char **argv)
{
	/* Allocate an integer from shared memory area */
	counter = SHM_ALLOC_TYPE(*counter);

	set_backend_get(on_get);
	wait_for_requests();
}
```

## Verification

Let's benchmark a synthetic response from the counter value:

```sh
$ ./wrk -t16 -c16 -d15s http://127.0.0.1:8080/scounter
Running 15s test @ http://127.0.0.1:8080/scounter
  16 threads and 16 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    34.19us   11.60us   1.75ms   91.72%
    Req/Sec    29.02k   679.91    30.40k    83.24%
  6976000 requests in 15.10s, 1.33GB read
Requests/sec: 461985.87
Transfer/sec:     90.18MB
```

Much better latency and higher RPS.

```sh
$ curl -D - http://127.0.0.1:8080/scounter
HTTP/1.1 200 OK
Date: Sat, 16 Sep 2023 19:54:53 GMT
Server: Varnish
X-Varnish: 6959264
Content-Type: text/plain
Content-Length: 20
Accept-Ranges: bytes
Connection: keep-alive

Hello 6976002 World!
```

The counter was executed manually once before the benchmark, so the numbers add up exactly!
