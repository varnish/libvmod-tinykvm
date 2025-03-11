# TCP Example

WARNING: This feature is _experimental_ and should not be used in a setting where, for example, a slow build-up of file descriptors is a problem. Each TCP connection that is taken and upgraded gets transferred to an epoll-loop, and more testing and scrutiny is needed in order to prove that the loop closes connections in all cases. Because these streams are TCP-based, `TIME_WAIT` is also a factor.

## Upgrading a connection

On HTTP it is possible to upgrade a connection in order to start serving using a different protocol on top of TCP.

Using tinykvm.steal() and providing a program and an optional argument we can take over the TCP stream used by Varnish in order to serve another purpose. This is an experimental feature.

## Limitations

It is not currently possible to inherit the OpenSSL session for encrypted endpoints. As a workaround one may handshake again.

WARNING: This feature is _experimental_.

## Example

```vcl
sub vcl_recv {
	if (req.url == "/tcp") {
		if (tinykvm.steal("hello", req.url)) {
			return (synth(123));
		}
		return (synth(500));
	}
}
```

Here we take over the connection and forward it to the `hello` program for the `/tcp` URL. The argument we pass to the program is optional and can be any string depending on the what the program expects.

In the program itself we can implement the socket callbacks like so:

```C++
static const char response[] =
	"HTTP/1.1 200 OK\r\n"
	"Server: Varnish Cache Edgerprise\r\n"
//	"Connection: Close\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 13\r\n"
	"\r\n"
	"Hello World!\n";

static int
on_connected(int fd, const char *remote, const char *argument)
{
	Print("* FD %d connected. Remote: %s Arg: %s\n",
		fd, remote, argument);

	write(fd, response, sizeof(response)-1);
	return 1;
}
static void
on_read(int fd, const uint8_t *data, size_t bytes)
{
	Print("* FD %d data: %p, %zu bytes\n", fd, data, bytes);

	/* Assume request */
	write(fd, response, sizeof(response)-1);
}
/*static void
on_writable(int fd)
{
	Print("* FD %d writable (again)\n", fd);

	write(fd, "Last bit\n", 9);
	shutdown(fd, SHUT_RDWR);
}*/
static void
on_disconnect(int fd, const char *reason)
{
	Print("* FD %d disconnected: %s\n", fd, reason);
}

int main(int argc, char **argv)
{
	Print("Hello Compute World!\n");

	set_backend_get(on_get);
	set_socket_on_connect(on_connected);
	set_socket_on_read(on_read);
	//set_socket_on_writable(on_writable);
	set_socket_on_disconnect(on_disconnect);
	wait_for_requests();
}
```

By hooking up the socket callbacks we can read and write data to the remotely connected party, using the underlying TCP connection.

## Demo

[Inspect the response in your browser](http://89.162.68.187:8080/tcp) 

_NOTE: Uses port 80, which may be closed._

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
