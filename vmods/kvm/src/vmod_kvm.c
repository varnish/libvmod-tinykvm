/**
 * @file vmod_kvm.c
 * 
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief 
 * @version 0.1
 * @date 2022-07-23
 * 
 * This file is the entry point for all VMOD KVM interaction with VCL.
 * The starting point is often kvm.embed_tenants, which is implemented
 * as vmod_embed_tenants by the VCC generator script.
 * 
 * The second most important function is vm_backend, which is implemented
 * in kvm_backend.c, with the entry point at the bottom.
 * 
 */
#include "vmod_kvm.h"

#include <vsb.h>
#include <vcl.h>
#include "vcc_if.h"
#include <malloc.h>

VCL_BOOL vmod_tenant_is_ready(VRT_CTX, VCL_PRIV task, VCL_STRING tenant)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (tenant == NULL || tenant[0] == 0)
	{
		VRT_fail(ctx, "kvm.tenant_is_ready() requires a tenant name");
		return (0);
	}

	struct vmod_kvm_tenant *tenptr = kvm_tenant_find(task, tenant);

	if (tenptr == NULL)
	{
		VRT_fail(ctx, "No such tenant: %s", tenant);
		return (0);
	}

	return (kvm_tenant_gucci(tenptr, 0));
}

VCL_VOID vmod_embed_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING json)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (json == NULL || json[0] == 0) {
		VRT_fail(ctx, "kvm.embed_tenants() requires a JSON string");
		return;
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	kvm_init_tenants_str(ctx, task, "Embedded JSON", json, strlen(json));
}

VCL_VOID vmod_load_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING filename)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (filename == NULL || filename[0] == 0) {
		VRT_fail(ctx, "kvm.load_tenants() requires a filename");
		return;
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	kvm_init_tenants_file(ctx, task, filename);
}

#include <curl/curl.h>

struct MemoryStruct
{
	char *memory;
	size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	char *ptr = realloc(mem->memory, mem->size + realsize + 1);
	if (!ptr)
	{
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

VCL_BOOL vmod_fetch_tenants(VRT_CTX, VCL_PRIV task, VCL_STRING url)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (url == NULL || url[0] == 0) {
		VRT_fail(ctx, "kvm.fetch_tenants() requires a URL");
		return (0);
	}

	/* Initialize, re-initialize and remove VMODs */
	initialize_vmods(ctx, task);

	CURL *curl_handle;
	CURLcode res;

	struct MemoryStruct chunk = {
		.memory = malloc(1),
		.size = 0
	};
	if (chunk.memory == NULL) {
		VRT_fail(ctx, "kvm.fetch_tenants(): Out of memory");
		return (0);
	}

	curl_handle = curl_easy_init();
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);

	res = curl_easy_perform(curl_handle);
	if (res != CURLE_OK)
	{
		VRT_fail(ctx, "kvm.fetch_tenants(): cURL failed: %s",
				 curl_easy_strerror(res));
		return (0);
	}

	kvm_init_tenants_str(ctx, task, url, chunk.memory, chunk.size);

	curl_easy_cleanup(curl_handle);
	free(chunk.memory);

	return (1);
}
