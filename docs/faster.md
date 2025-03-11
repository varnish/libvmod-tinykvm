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
