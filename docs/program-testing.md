# Testing programs

Programs in the TinyKVM VMOD are running in KVM, which again runs on the native system CPU. Because of this, each program is also executable on the users system. Indeed, we can develop the programs on our own systems (matching the servers architecture), and branch on whether or not we are in the sandbox.

## Testing on your system

```cpp
template <bool KVM>
void produce_image(const nlohmann::json& j,
	std::string_view content_type,
	const uint8_t *source_image, const size_t source_image_len)
{
	/* WebP image encoding here. */
	...

	if constexpr (KVM)
	{
		...

		/* Varnish Enterprise image output */
		Backend::response(200, "image/webp", buffer.data(), buffer.size());
	}
	else
	{
		static int counter = 0;
		/* Linux userspace WebP image output */
		write_file("output" + std::to_string(counter++) + ".webp", buffer);
	}
}

template <bool KVM> static void
on_post(const char *url, const char *arg, const char *ctype, const uint8_t *src, size_t len)
{
	const auto j = nlohmann::json::parse(arg, arg + strlen(arg), nullptr, true, true);
	produce_image<KVM>(j, ctype, src, len);
}

int main(int, char** argv)
{
	if (IS_LINUX_MAIN())
	{
		// Test1: Convert JPEG to WebP
		auto f1 = load_file("../../assets/rose.jpg");
		on_post<false>("", "{}", "image/jpeg", f1.data(), f1.size());

		// Test2: Convert WebP to WebP
		auto f2 = load_file("output0.webp");
		on_post<false>("", "{}", "image/webp", f2.data(), f2.size());

		return 0;
	}

	set_backend_get(on_get);
	set_backend_post(on_post);
	set_on_error(on_error);
	wait_for_requests();
}
```

In this example the WebP program calls the same function as the HTTP POST method when executed inside a sandbox. Using this, we can inspect the images and perhaps use an image comparison tool to calculate the difference in qualities between the input and output.

Running the WebP transcoder program directly on my machine produces these:
```sh
$ ls *.webp
output0.webp  output1.webp
```

## Testing in Varnishtest

Varnish has a tool called varnishtest which can be used to test programs. The TinyKVM VMOD has two batteries of tests that cover a wide area of code.

A typical TinyKVM VMOD test will compile a static program from a simple C program that includes the `kvm_api.h` header. See this synthetic response test:

```vtc
varnishtest "Compute: Synthetic response"

feature cmd "ls /dev/kvm"

shell {
cat >synth.c <<-EOF
#include "kvm_api.h"

static void on_get(const char *url, const char *arg)
{
	backend_response_str(200, "text/plain", "Hello World");
}

int main(int argc, char **argv)
{
	set_backend_get(on_get);
	wait_for_requests();
}
EOF
gcc -static -O2 synth.c -I${testdir} -o synth
}

varnish v1 -vcl+backend {
vcl 4.1;
	import tinykvm;
	backend default none;

	sub vcl_init {
		tinykvm.configure("test1",
			"""{
				"filename": "${tmpdir}/synth"
			}""");
	}

	sub vcl_recv {
		return (pass);
	}

	sub vcl_backend_error {
		if (bereq.url == "/1") {
			# Status=0 => Inherit from program
			tinykvm.synth(0, "test1");
			return (deliver);
		} else if (bereq.url == "/2") {
			# Status=500 => Override status from program
			tinykvm.synth(501, "test1");
			return (deliver);
		}
	}
} -start

client c1 {
	txreq -url "/1" -hdr "Host: test"
	rxresp
	expect resp.body == "Hello World"
	expect resp.status == 200

	txreq -url "/2" -hdr "Host: test"
	rxresp
	expect resp.body == "Hello World"
	expect resp.status == 501
} -run
```

The test makes use of a feature test to enable it only when `/dev/kvm` is present.

The program `test1` is defined inline through the use of `tinykvm.configure(name, config)`. A program only needs one way for the Compute loader to find a program. So you will need to specify either a filename or a URI where the program can be found.
