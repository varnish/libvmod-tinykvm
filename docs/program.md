# Program Example

In order to build a program in any language, we will need the KVM API. The KVM API can be found distributed with the VMOD. The single-file API is called `kvm_api.h`. Using that file we can implement APIs for any other systems language, and several others, such as Go, Kotlin, Nim and so on.

A simple program needs to do at least two things:
- Subscribe to a HTTP method, such as GET, POST or *a request*
- Wait for requests

If the program skips one of those steps, it is invalid, and it will fail initialization. It is _the only mandatory difference_ between a normal Linux program and a TinyKVM program.

```c
#include "kvm_api.h"
#include <string.h>
#include <stdio.h>

static void
on_get(const char *url, const char *arg)
{
	backend_response_str(200, "text/plain", "Hello Compute World!");
}

int main(int argc, char **argv)
{
	/* Macro to check if program is run from terminal. */ 
	if (IS_LINUX_MAIN()) {
		puts("Hello Linux World!");
		return 0;
	}

	set_backend_get(on_get);
	wait_for_requests();
}
```

This hello world program hooks up the GET method callback and waits for requests. It fulfills both (and the only) requirements.

When a request comes in, `on_get` will get called, and it will deliver a 200 OK response saying `Hello World!`.

We don't need to elaborate further on GET requests for C programs as including and linking libraries does not belong here. The `kvm_api.h` file contains all the API functions with inline commentary.

## Verification

```sh
$ curl http://127.0.0.1:8080/hello
Hello Compute World!
```

It works as expected!

We can also run the program locally in the terminal:

```sh
$ ./hello
Hello Linux World!
```

## Building

When building a program for the TinyKVM VMOD there are two requirements:

1. Use the API that comes with the VMOD. The API allows you to hook into events that you care about for your program, such as handling GET and POST.

2. The program must be built statically.

The simplest check to see if a program is statically built is to use ldd:
```sh
$ ldd .build/thumbnails 
	not a dynamic executable
```
Because this program is not dynamic, it is statically built, and so we can now use this program with the TinyKVM VMOD.

Example:
```sh
$ gcc -O2 -Wall -static hello_world.c -o hello
$ ldd hello
	not a dynamic executable
```

The compiler argument `-static` ensures the final executable is static and uses absolute addresses internally.

## Using it locally in Varnish

Create a `tinykvm.vcl`:

```vcl
vcl 4.1;
import tinykvm;

backend default none;

sub vcl_init {
	# Tell TinyKVM how to contact Varnish (Unix Socket *ONLY*).
	tinykvm.init_self_requests("/tmp/tinykvm.sock");

	# Create new program inline in VCL.
	tinykvm.configure("my_program",
		"""{
			"filename": "/home/user/github/my_program/my_program"
		}""");

	tinykvm.start("my_program");
}

sub vcl_backend_fetch {
	# Utilize the new program.
	set bereq.backend = tinykvm.program("my_program", bereq.url);
}
```

Start varnishd with a UDS listener:

```
varnishd -a :8080 -a /tmp/tinykvm.sock -f $PWD/tinykvm.vcl -n /tmp/varnishd -F

Debug: Version: varnish-plus-6.0.11r6 revision b5ced394bab070f7850be3fb5ca6e464c12e88b9
Debug: Platform: Linux,5.19.0-46-generic,x86_64,-jnone,-sdefault,-sdefault,-hcritbit
Debug: Child (1913083) Started
Child launched OK
Info: Child (1913083) said Child starts
Info: Child (1913083) said Program 'my_program' is loaded (local, not cached, vm=2, huge=0/0, ready=0.97ms)
```

## Fast iteration for development

Create a `tinykvm.vcl`:

```vcl
vcl 4.1;
import tinykvm;

backend default none;

sub vcl_init {
	# Tell TinyKVM how to contact Varnish (Unix Socket *ONLY*).
	tinykvm.init_self_requests("/tmp/tinykvm.sock");

	# Create new program inline in VCL.
	tinykvm.configure("my_program",
		"""{
			"filename": "/home/user/github/my_program/my_program"
		}""");
}

sub vcl_backend_fetch {
	# Unload any previously loaded program
	tinykvm.invalidate_programs();
	# Utilize the new program, always reloading it from disk.
	set bereq.backend = tinykvm.program("my_program", bereq.url);
}
```

Now every time you make a request to Varnish the program will be automatically reloaded from disk. Use this when working on a program.

## Uploading

When uploading a program to make it available to a range of Varnish instances through URL fetches, it is important that the program is compressed, drastically reducing startup delays.

Varnish ships with a custom gzip, so we cannot use `.tar.gz`, but we can use XZ. If you have built your program already, compress it with tar using -J:

```sh
BIN=myfilebin

upload_program() {
	base=`basename $1`
	tar -cJf - $1 | curl -H "Host: filebin.varnish-software.com" --data-binary "@-" -X POST https://filebin.varnish-software.com/$BIN/$base.tar.xz
}

upload_program src/myprogram
```

In this example we use filebin to store the programs. Filebins expire after a while, and requires the owners permission to use. However, this example shows how we can automate building and uploading, while also compressing the program to minimize delays.

The script will upload `myprogram.tar.xz` to the given bin. If you set your programs uri to `.../myprogram.tar.xz`, it will automatically decompress and load it.

## Live-updating

It's possible to send a program directly to a Varnish instance, where it will be reloaded and keep any previous state. A so-called live update. In order to do this, you will need a live update end-point in your VCL:

```
	if (bereq.url == "/update") {
		set bereq.backend = tinykvm.live_update(bereq.http.Host, bereq.http.X-LiveUpdate);
	}
```
What this end-point does is update a program specified in the Host header with a key from the X-LiveUpdate HTTP header. As an example, this is how I live updated my WebP encoder:

```sh
$ curl -D - -H "X-LiveUpdate: 123" -H "Host: webp" --data-binary "@.build/webpencoder" -X POST http://127.0.0.1:8080/update
```

And I made it a part of the build script of the WebP encoder to simplify things, so that on a successful build it would automatically replace the one in my Varnish instance with the new build:

```sh
#!/usr/bin/env bash
set -e
mkdir -p .build
pushd .build
cmake .. -G Ninja
ninja
popd

file=".build/webpencoder"
tenant="webp"
key="123"
host="127.0.0.1:8080"

curl -H "X-LiveUpdate: $key" -H "Host: $tenant" --data-binary "@$file" -X POST http://$host/update
```

When working on a program that is nearly there, this will save a lot of time!
