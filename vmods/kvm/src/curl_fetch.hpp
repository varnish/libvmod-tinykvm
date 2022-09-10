#pragma once
#include <cstddef>

struct MemoryStruct
{
    char*  memory;
    size_t size;
};

extern "C" {
int kvm_curl_fetch(
	const char *url, void(*callback)(void*, MemoryStruct *), void *usr);
}
