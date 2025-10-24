#include "curl_fetch.hpp"

#include <curl/curl.h>
#include <cstring>
#include <malloc.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "varnish.hpp"
extern "C" {
#include "vtim.h"
}
typedef size_t (*write_callback)(char *, size_t, size_t, void *);
namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);
}
static const bool VERBOSE_CURL_FETCH = false;

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
int kvm_curl_fetch(
	const char *url, kvm_curl_callback callback, void *usr, const char *condhdr)
{
	struct curl_slist *req_list = NULL;
	int retvalue = -1;
	const size_t url_len = strlen(url);

	MemoryStruct chunk {
		.memory = (char *)malloc(1),
		.size = 0
	};
	if (chunk.memory == NULL || url_len < 8u) {
		return (-1);
	}

	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (write_callback)kvm_WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

	const bool is_http = (memcmp(url, "http", 4) == 0);
	if (is_http)
	{
		/* Many URLs go straight to redirects, and it is disabled by default. */
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

		if (condhdr != nullptr && condhdr[0] != 0) {
			//printf("Adding header: %s\n", condhdr);
			req_list = curl_slist_append(req_list, condhdr);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_list);
		}
	}

	CURLcode res = curl_easy_perform(curl);

	long status;
	if (is_http) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	} else {
		status = (res == CURLE_OK) ? 200 : -1;
	}

	curl_slist_free_all(req_list);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		VSL(SLT_Error, 0,
			"kvm.curl_fetch(): cURL failed for '%s': %s", url, curl_easy_strerror(res));
		fprintf(stderr,
			"kvm.curl_fetch(): cURL failed for '%s': %s\n", url, curl_easy_strerror(res));
		retvalue = -1;
	}
	else {
		try {
			callback(usr, status, &chunk);
			retvalue = 0;
		}
		catch (const std::exception& e) {
			VSL(SLT_Error, 0,
				"kvm.curl_fetch(): cURL failed: %s", e.what());
			fprintf(stderr,
				"kvm.curl_fetch(): cURL failed: %s\n", e.what());
			retvalue = -1;
		}
	}

	free(chunk.memory);

	return (retvalue);
}

extern "C"
int kvm_curl_fetch_into_file(const char *url, const char *filepath)
{
	std::string filename_mtime = "";
	struct stat st;
	if (stat(filepath, &st) == 0) {
		char buf[32];
		VTIM_format(st.st_mtim.tv_sec, buf);
		filename_mtime = "If-Modified-Since: " + std::string(buf);
	}

	struct CurlData {
		std::string uri;
		std::string filepath;
		int status;
	} data {
		.uri = url,
		.filepath = filepath,
		.status = 0
	};

	return kvm_curl_fetch(url,
	[] (void* usr, long status, MemoryStruct* chunk) {
		auto* data = (CurlData *)usr;
		data->status = status;
		if (status == 304) {
			if constexpr (VERBOSE_CURL_FETCH) {
				printf("kvm_curl_fetch: Newer '%s' on disk\n",
					data->filepath.c_str());
			}
			return;

		} else if (status == 200) {
			if constexpr (VERBOSE_CURL_FETCH) {
				printf("kvm_curl_fetch: Downloading to '%s' from %s\n",
					data->filepath.c_str(), data->uri.c_str());
			}

			FILE *f = fopen(data->filepath.c_str(), "wb");
			if (!f) {
				throw std::runtime_error("Failed to open file for writing: " + data->filepath);
			}
			if (fwrite(chunk->memory, 1, chunk->size, f) != chunk->size) {
				fclose(f);
				throw std::runtime_error("Failed to write to file: " + data->filepath);
			}
			fclose(f);

		} else {
			// Unhandled HTTP status?
			throw std::runtime_error("Fetching program '" + data->filepath + "' failed. URL: " + data->uri);
		}

	}, &data, filename_mtime.c_str());
}
