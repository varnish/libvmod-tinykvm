# Known issues

> It is not possible to inherit the OpenSSL session when taking over / upgrading a HTTP connection.

Known problem. Probably something that can be solved in a future version.

> Multiprocessing support?

There is multiprocessing support already, however KVM currently does not let you remove vCPUs, and so it's very hard to take back these resources without rebuilding the VM. For a long-running demanding task this is of course possible, but maybe not appropriate for an (often) edge-tier HTTP cache.

I think that in the future there will at least be multiprocessing support for the storage VM which is one per program, and this scaling might be acceptable.

The immediate advice is to find libraries that make use of AVX-512 which is very likely available on your server, and improve performance by fully utilizing what the CPU has to offer.

> GPU support?

Perhaps OpenCL support in some form or another. We don't really see anyone having GPUs on their server though. Is this ultimately just to generate cat pictures?

Regardless, this is probably not too hard to achieve in a future version.

> There is seemingly dead time according to htop?

Yes, the way it works now is that every program has a queue of ready-to-go request VMs, and these are fairly handed out. During processing, especially when chaining programs, these programs are reserved as needed. When they are reserved, they can't be used by anyone else. We can imagine that one request is waiting for a request VM to become ready, and is sleeping. This indicates that some program is being used for many purposes and may benefit from increased concurrency. That is, increasing its number of request VMs.

> Enabling hugepages can crash the server.

If not enough hugepages are allocated beforehand, the Linux kernel will interrupt the process with a SIGBUS signal. In theory this can be handled gracefully, ending the program and unloading the VMs (as they cannot be used). This is currently not implemented.
