#include "curl_fetch.hpp"

#include <curl/curl.h>
#include <cstring>
#include <malloc.h>
#include "varnish.hpp"
typedef size_t (*write_callback)(char *, size_t, size_t, void *);

extern "C" size_t
kvm_WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	const size_t realsize = size * nmemb;
	MemoryStruct *mem = (MemoryStruct *)userp;

	char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
	if (!ptr) {
		/* Out of memory! Let's not try to print or log anything. */
		free(mem->memory);
		mem->memory = NULL;
		mem->size = 0;
		return 0;
	}

	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

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
		if (ctx != NULL && ctx->vsl != NULL)
			VSLb(ctx->vsl, SLT_Error, "kvm.curl_fetch(): Out of memory");
		return (-1);
	}

	curl_handle = curl_easy_init();
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, (write_callback)kvm_WriteMemoryCallback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);
	/* Many URLs go straight to redirects, and it is disabled by default. */
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);

	res = curl_easy_perform(curl_handle);
	if (res != CURLE_OK) {
		VSL(SLT_Error, 0,
			"kvm.curl_fetch(): cURL failed: %s", curl_easy_strerror(res));
		fprintf(stderr,
			"kvm.curl_fetch(): cURL failed: %s", curl_easy_strerror(res));
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
