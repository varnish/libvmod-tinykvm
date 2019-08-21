#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

// 1. connect using TCP socket and send requests
// 2. generate files for ESI parser
// 3. VCC fuzzing

extern void http_fuzzer(void* data, size_t len);
extern void http_fuzzer_server(void* data, size_t len);
extern void h2_fuzzer(void* data, size_t len);
extern void hpack_fuzzer(void* data, size_t len);
// varnishd has many many leaks.. can't enable this
int __lsan_is_turned_off() { return 1; }

int LLVMFuzzerTestOneInput(void* data, size_t len)
{
    http_fuzzer(data, len);
    //http_fuzzer_server(data, len);
    //h2_fuzzer(data, len);
    //hpack_fuzzer(data, len);
    return 0;
}
