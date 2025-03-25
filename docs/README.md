# Introduction

> The TinyKVM VMOD extends Varnish Cache with compute capabilities using native performance specialized virtual machines.

That is the official wording, at least. A simpler explanation is that it takes a normal Linux program, isolates it, gives it some hyper-focused and safe access to Varnish, and gives the program an ability to handle data and produce responses safely.

TinyKVM VMOD programs are processing data the same way any of your normal Linux programs would. Instead of opening files and reading/writing to standard pipes, we instead get a request from Varnish, and we produce a regular HTTP response. Many existing programs are tested and verified locally on the terminal, with the usual access to developer tools.

The foundation for this VMOD is the same foundation used by the public cloud, which is KVM. The most trusted, most robust and battle hardened hardware virtualization API, with an amazingly low attack surface. However, in our case the attack surface is even smaller than that, as there is fundamentally no I/O, requiring no drivers which is a source of overhead and attack surface for QEMU. _[TinyKVM paper](https://github.com/varnish/libvmod-tinykvm/releases/download/v0.1/Introducing_TinyKVM_Alf_2023__CloudCom.pdf)._


## Navigation

[Getting Started](getting-started.md) with the TinyKVM VMOD.

[VCL Setup](program.md)

[VCL Examples](vcl-example.md)

[Processing Data](processing.md)

[Program Storage](program-storage.md)

[Testing Programs](program-testing.md)

[Direct Invocations](direct.md)

[Remote Requests](remote-request.md)

[Self-Requests](self-request.md)

[Glossary](glossary.md)

[VMOD Reference](vmod.md)

[Monitoring](monitoring.md)

[Live Statistics](statistics.md)

[Building New Programs](program.md)

[Program With Storage](storage.md)

[Known Issues](issues.md)

[Performance Tips](faster.md)


## What can it be used for, right now?

The TinyKVM VMOD allows Varnish to process data safely at the speed of the native CPU.

- Convert any image to more efficient formats, eg. WebP or AVIF
- Resize, watermark images or generate thumbnails
- Generate speech audio from text using any library or neural net
- Generate answers from text using any neural net
- Utilize any compression format, eg. Zstandard
- Minify JavaScript or JSON
- Validate and manipulate data formats like JSON or XML

Just like a program running on Linux, the programs for the TinyKVM VMOD will only have to be built once, as the user-facing API evolves only by addition. Lastly, the TinyKVM VMOD trivializes adding new compute-related features to Varnish in a safe way. Regular VMODs are hard to create, require a lot of scrutiny, and have a wide array of footguns attached due to the aging framework they are built on.

<img src="public/watermark.png" alt="Watermark PNG">

_An image processed by the watermarking program_


## Example program

This snippet is from a simple program that takes an input, processes it, and delivers a result:

```cpp
static void on_post(const char *, const char *,
	const char *content_type, const uint8_t* data, const size_t len)
{
    /* Minify the JSON at 6GB/s */
    size_t dst_len = DST_BUFFER;
    simdjson::minify((const char *)data, len, dst, dst_len);

    /* Respond with the minified JSON */
    Backend::response(200, "application/json", {dst, dst_len});
}
```
JSON minification is 3 lines of code. If the program crashes or gets stuck it will always safely respond with the original JSON instead, due to a second-chance as part of standard error handling.

```cpp
static void on_post(const char *, const char *,
	const char *ctype, const uint8_t *xml, size_t len)
{
	tinyxml2::XMLDocument doc;
	doc.Parse((const char*)xml, len);

	if (doc.Error())
	{
		Http::set(BERESP, "X-ErrorLine: " + std::to_string(doc.ErrorLineNum()));
		Backend::response(503, "text/plain", doc.ErrorStr());
	}

	Backend::response(200, "text/xml", std::string_view(xml, len));
}
```
XML validation in a few lines of code. It will explain where the validation fails. If validation succeeds, the original XML is passed on.


## Prerequisites

Check that the KVM device exists:
```sh
stat /dev/kvm
```
Most cloud vendors have nested virtualization enabled (or some way to enable it).

It is not required that a container be run as privileged in order to forward `/dev/kvm` to it, you can pass `--device /dev/kvm`.

If `/dev/kvm` is not available but should be, check if hardware virtualization is disabled in BIOS. Sometimes it's named VMX (Intel) or SVM (AMD).


Add your user (or the `varnish` user) to the `kvm` group:

```sh
sudo addgroup $USER kvm
```

With membership in this group you can run KVM without sudo privileges.
