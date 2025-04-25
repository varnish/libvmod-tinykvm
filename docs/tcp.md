# TCP Example

WARNING: This feature is _experimental_ and should not be used in a setting where, for example, a slow build-up of file descriptors is a problem. Each TCP connection that is taken and upgraded gets transferred to an epoll-loop, and more testing and scrutiny is needed in order to prove that the loop closes connections in all cases. Because these streams are TCP-based, `TIME_WAIT` is also a factor.

## Upgrading a connection

On HTTP it is possible to upgrade a connection in order to start serving using a different protocol on top of TCP. In VCL `return (pipe)` should be used to forward the request to eg. `127.0.0.1:8081` where the epoll system is listening:

```vcl
backend default {
	.host = "127.0.0.1";
	.port = "8081";
	.connect_timeout = 1s;
}

sub vcl_init {
	tinykvm.library("https://filebin.varnish-software.com/tinykvm_programs/compute.json");
	# Start the TCP example
	tinykvm.start("hello");
}

sub vcl_recv {
	if (req.http.Upgrade) {
		# Use KVM epoll backend
		return (pipe);
	}
}
```
In order for this to work the program must also be started. You can use `tinykvm.start("program")` in `vcl_init`.

## Example program

```cpp
static const char response[] =
	"HTTP/1.1 200 OK\r\n"
	"Server: Varnish Cache Edgerprise\r\n"
//	"Connection: Close\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 13\r\n"
	"\r\n"
	"Hello World!\n";

static void
on_socket_prepare(int thread)
{
	std::vector<kvm_socket_event> write_events;
	while (true) {
		std::array<kvm_socket_event, 16> events;
		int cnt = wait_for_socket_events_paused(events.data(), events.size());
		for (int i = 0; i < cnt; ++i) {
			auto& se = events[i];
			switch (se.event) {
			case SOCKET_CONNECT:
				//Print("Socket %d connected: %s\n", se.fd, se.remote);
				break;
			case SOCKET_READ:
				//Print("Socket %d read: %zu bytes\n", se.fd, se.data_len);
				break;
			case SOCKET_WRITABLE:
				//Print("Socket %d writable\n", se.fd);
				/* Write to the socket. */
				write_events.push_back({
					.fd = se.fd,
					.event = SOCKET_WRITABLE,
					.remote = nullptr,
					.arg = nullptr,
					.data = (const uint8_t *)response,
					.data_len = sizeof(response) - 1
				});
				break;
			case SOCKET_DISCONNECT:
				//Print("Socket %d disconnected: %s\n", se.fd, se.remote);
				break;
			}
		}
		if (!write_events.empty()) {
			/* Write to the socket. */
			sys_sockets_write(write_events.data(), write_events.size());
			write_events.clear();
		}
		/* Continue waiting for events. */
	}
}

int main(int argc, char **argv)
{
	Print("Hello Compute %s World!\n", getenv("KVM_TYPE"));

	set_backend_get(on_get);

	set_socket_prepare_for_pause(on_socket_prepare);
	wait_for_requests();
}
```

By hooking up the socket prepare callback we can receive events on connected TCP sockets.

## Demo

[Inspect the response in your browser](http://127.0.0.1:8081)

## Verification

```
$ curl -D - http://127.0.0.1:8080/tcp
HTTP/1.1 200 OK
Server: Varnish Cache Edgerprise
Content-Type: text/plain
Content-Length: 13

Hello World!
```

Fetching the URL with curl, we can see that something different happened. An unusual server named `Varnish Cache Edgerprise` responded.

## Benchmarks

```sh
$ ./wrk -c128 -t128 http://127.0.0.1:8081
Running 10s test @ http://127.0.0.1:8081
  128 threads and 128 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    55.62us  112.62us   8.43ms   99.34%
    Req/Sec    19.64k     6.07k   53.80k    70.65%
  25264695 requests in 10.10s, 2.64GB read
Requests/sec: 2500897.32
Transfer/sec:    267.12MB
```

The TCP feature is the fastest networking possible, reaching 2.5M req/s on my machine.
