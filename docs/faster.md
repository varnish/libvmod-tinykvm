# Going faster

In order to process data in the fastest way possible, there are some basic steps we can take:

1. Make use of the features your CPU already has. Check CPU features: `cat /proc/cpuinfo`.
2. Pick the fastest (SIMD-) library for the given task, and build it with the server CPU in mind.
3. Use appropriate algorithms. Eg. don't decompress to RGB for image transcoding, it can sometimes be directly decoded to YUV and then encoded to the new format, losing less precision in the process.
4. Disable ephemeralness (complete wipe after each request). This is only OK to do if there are no security concerns and you trust and verify the program.
5. Enable experimental multi-procesing and utilize more vCPUs to solve the problem. Requires programming against the multi-processing APIs. There is no multi-processing support for threads in v1.

The first two points is usually enough. If you need even more performance, we can go slightly more crazy:

1. Pick the fastest experimental library, and disable all checks.
2. For security reasons, ensure ephemeralness is enabled (resets), and disable mutable storage.

This is as fast as it goes. Usually we don't care what goes on inside the program as they are specifically designed to finish in time, and to be completely wiped after each request. The one concern might be that it can produce wrong data, but that's experimental libraries for you.

## Improving the performance of an AVIF transcoder

Getting better performance out of the AVIF transcoder took a long time, but the most impactful change was to change codec to DAV1D. For reference, it used to transcode 1500-ish example images per second.

The second change was to inspect the pagetables of the AVIF program by enabling `verbose_pagetables`. Using that, I could see that the first executable 2MB segment had a 4K page in front which likely contained ELF program headers (or other things). Simply moving the text segment backwards enabled this impactful 2MB page to become properly aligned, which reduces pagewalks.

```cmake
target_link_libraries(avifencode -Wl,-Ttext-segment=0x1FF000)
```
Now the first 2MB page is not split into 4KB leaf pages.

Third, I enabled `-march=native -Ofast -fno-fast-math` optimizations, which can be hit or miss.

And lastly, I enabled hugepages for the main VM and the forks. I also disabled ephemeral:
```
	"ephemeral": false,
	"hugepages": true,
	"request_hugepages": true,
```

### Benchmarks

Without hugepages:

```sh
$ ./wrk -c32 -t32 http://127.0.0.1:8080/avif/bench
Running 10s test @ http://127.0.0.1:8080/avif/bench
  32 threads and 32 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    15.38ms    1.41ms  51.19ms   90.48%
    Req/Sec    65.03      5.63    80.00     94.47%
  20859 requests in 10.05s, 213.90MB read
Requests/sec:   2076.37
Transfer/sec:     21.29MB
```

Hugepages for requests only:
```sh
$ ./wrk -c32 -t32 http://127.0.0.1:8080/avif/bench
Running 10s test @ http://127.0.0.1:8080/avif/bench
  32 threads and 32 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    15.23ms    1.37ms  49.22ms   91.11%
    Req/Sec    65.75      5.41    90.00     60.31%
  21099 requests in 10.06s, 216.36MB read
Requests/sec:   2097.66
Transfer/sec:     21.51MB
```

Hugepages for main VM and request VMs:
```sh
$ ./wrk -c32 -t32 http://127.0.0.1:8080/avif/bench
Running 10s test @ http://127.0.0.1:8080/avif/bench
  32 threads and 32 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    15.16ms    1.34ms  48.40ms   90.50%
    Req/Sec    66.00      5.29    90.00     62.56%
  21174 requests in 10.06s, 217.13MB read
Requests/sec:   2105.72
Transfer/sec:     21.59MB
```

Conclusion: We improved performance in all cases with hugepages, but using hugepages only for the request VMs had the most impact. The brunt of our performance came from selecting another codec from the default. Overall, performance improved from 1500 to 2100 example images/sec.
