#include "kvm_backend.h"

#include <curl/curl.h>
#include <cstring>
#include <exception>
#include <malloc.h>
#include "varnish.hpp"
#include "kvm_settings.h"
typedef size_t (*write_callback)(char *, size_t, size_t, void *);

struct MemoryStruct {
    char*  memory;
    size_t size;
};

extern "C" size_t
kvm_SelfRequestCallback(void *contents, size_t size, size_t nmemb, void *userp)
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

static void set_error_result(backend_result *result, uint16_t status)
{
	result->status = status;
	result->content_length = 0;
	result->bufcount = 0;
	result->type = "";
	result->tsize = 0;
}

extern "C"
int kvm_self_request(VRT_CTX, const char *c_path, backend_result *result)
{
	struct curl_slist *req_list = NULL;
	int retvalue = -1;
	const std::string path = c_path;

	MemoryStruct chunk {
		.memory = (char *)malloc(1),
		.size = 0
	};
	if (chunk.memory == NULL || path.size() < 1u) {
		set_error_result(result, 500);
		return (-1);
	}

	if (kvm::self_request_concurrency++ >= kvm_settings.self_request_max_concurrency)
	{
		kvm::self_request_concurrency--;
		set_error_result(result, 500);
		return (-1);
	}

	std::string url;
	if (path[0] == '/')
		url = kvm::self_request_prefix + path;
	else
		url = path;

	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (write_callback)kvm_SelfRequestCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

	if (!kvm::self_request_uri.empty())
	{
		if (int err = curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, kvm::self_request_uri.c_str()) != CURLE_OK) {
			set_error_result(result, 500);
			curl_easy_cleanup(curl);
			kvm::self_request_concurrency--;
			return (-1);
		}
	}

	/* Many URLs go straight to redirects, and it is disabled by default. */
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

	CURLcode res = curl_easy_perform(curl);

	long status;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

	if (res != CURLE_OK) {
		VSLb(ctx->vsl, SLT_Error,
			"kvm.curl_fetch(): cURL failed for '%s': %s",
			url.c_str(), curl_easy_strerror(res));
		fprintf(stderr,
			"kvm.curl_fetch(): cURL failed for '%s': %s\n",
			url.c_str(), curl_easy_strerror(res));

		free(chunk.memory);

		set_error_result(result, status);

		retvalue = -1;
	}
	else {
		const char *curl_ctype = nullptr;
		res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &curl_ctype);
		if (curl_ctype) {
			result->tsize = strlen(curl_ctype);
			char *ctype = (char*)WS_Alloc(ctx->ws, result->tsize + 1);
			if (ctype != nullptr) {
				std::memcpy(ctype, curl_ctype, result->tsize + 1);
				result->type = ctype;
			} else {
				result->type  = "";
				result->tsize = 0;
			}
		} else {
			result->type  = "";
			result->tsize = 0;
		}

		result->status = status;
		// XXX: chunk.memory needs to be freed outside
		result->content_length = chunk.size;
		result->buffers[0].data = chunk.memory;
		result->buffers[0].size = chunk.size;
		result->bufcount = 1;

		retvalue = 0;
	}

	kvm::self_request_concurrency--;
	curl_slist_free_all(req_list);
	curl_easy_cleanup(curl);

	return (retvalue);
}

extern "C"
int kvm_set_self_request(VRT_CTX, VCL_PRIV, const char *uri, const char *prefix,
	long max_concurrent_requests)
{
	(void)ctx;

	kvm::self_request_uri = uri;
	kvm::self_request_prefix = prefix;
	kvm_settings.self_request_max_concurrency = max_concurrent_requests;

	if (kvm::self_request_uri.empty()) {
		return (1);
	}
	/* Remove the last /, as self-requests are required to start with it. */
	if (kvm::self_request_uri.back() == '/')
		kvm::self_request_uri.pop_back();
	return (1);
}
