#include "curl_fetch.hpp"

#include <curl/curl.h>
#include <cstring>
#include <malloc.h>
#include "varnish.hpp"

namespace kvm {
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	MemoryStruct *mem = (MemoryStruct *)userp;

	char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
	if (!ptr) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}
} // kvm

extern "C"
int kvm_curl_fetch(const struct vrt_ctx *ctx,
	const char *url, void(*callback)(void*, MemoryStruct *), void *usr)
{
	CURL *curl_handle;
	CURLcode res;
	int retvalue = -1;

	MemoryStruct chunk {
		.memory = (char *)malloc(1),
		.size = 0
	};
	if (chunk.memory == NULL) {
		if (ctx != NULL)
			VRT_fail(ctx, "kvm.fetch_tenants(): Out of memory");
		return (-1);
	}

	curl_handle = curl_easy_init();
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, kvm::WriteMemoryCallback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);

	res = curl_easy_perform(curl_handle);
	if (res != CURLE_OK) {
		if (ctx != NULL)
			VRT_fail(ctx, "kvm.fetch_tenants(): cURL failed: %s",
				curl_easy_strerror(res));
		retvalue = -1;
	}
	else {
		callback(usr, &chunk);
		retvalue = 0;
	}

	curl_easy_cleanup(curl_handle);
	free(chunk.memory);

	return (retvalue);
}
