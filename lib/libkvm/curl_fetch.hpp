#pragma once
#include <cstddef>

struct MemoryStruct {
    char*  memory;
    size_t size;
};

typedef void (*kvm_curl_callback)(void *usr, long status, MemoryStruct *chunk);

extern "C" {
int kvm_curl_fetch(
	const char *url, kvm_curl_callback callback, void *usr, const char* condhdr = nullptr);
int kvm_curl_fetch_into_file(
	const char *url, const char *filepath);
}
