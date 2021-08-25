#include "../tenant.hpp"
#include "../machine_instance.hpp"
#include "../varnish.hpp"
#include <cassert>
#include <cstring>
#include <stdexcept>

typedef struct oaref oaref_t;
extern "C" {
# include <vqueue.h>
# include <cache/cache_vcl.h>
}
#include <curl/curl.h>
#undef curl_easy_getinfo
#include <dlfcn.h>

extern "C" {
	void* VMOD_Handle(struct vmod *);
	typedef struct vmod *(*vmod_handle_f) ();
	typedef struct vmod_priv *(*vmod_priv_handle_f) ();
}

namespace kvm {

#define vlookup(handle, x) \
	auto x = (x ##_t) dlsym(handle, #x)
inline auto* validate_deref(const char** ptr, const char* s) {
	if (ptr != nullptr)
		return *ptr;
	throw std::runtime_error("Invalid pointer for enum: " + std::string(s));
}
#define vmod_enum(handle, x) \
	validate_deref((const char **)dlsym(handle, #x), #x)

void initialize_vmod_http(VRT_CTX, VCL_PRIV task)
{
	auto* vcl = ctx->vcl;
	assert (vcl != nullptr);

	auto vmod_getter = (vmod_handle_f)dlsym(vcl->dlh, "vmod_http_handle");
	auto priv_getter = (vmod_priv_handle_f)dlsym(vcl->dlh, "vmod_priv_http_handle");

	if (vmod_getter == nullptr || priv_getter == nullptr) {
		printf("*** Curl dyncall: VMOD NOT FOUND\n");
		TenantConfig::reset_dynamic_call(task, "curl.fetch");
		return;
	}

	struct vmod *vmod_http = vmod_getter();
	struct vmod_priv *priv = priv_getter();
	assert(vmod_http != nullptr && priv != nullptr);

	auto* handle = VMOD_Handle(vmod_http);
	if (handle == nullptr) return;

	typedef CURL* (*curl_easy_init_t) (void);
	typedef CURLcode (*curl_easy_setopt_t)(CURL *, CURLoption, ...);
	typedef CURLcode (*curl_easy_perform_t)(CURL *);
	typedef CURLcode (*curl_easy_getinfo_t)(CURL *, CURLINFO, ...);
	typedef void (*curl_easy_cleanup_t)(CURL *);
	typedef size_t (*write_callback)(char *, size_t, size_t, void *);
	vlookup(handle, curl_easy_init);
	vlookup(handle, curl_easy_setopt);
	vlookup(handle, curl_easy_perform);
	vlookup(handle, curl_easy_getinfo);
	vlookup(handle, curl_easy_cleanup);

	TenantConfig::set_dynamic_call(task, "curl.fetch",
	[=] (MachineInstance& inst)
	{
		auto regs = inst.machine().registers();
		/* URL */
		std::string url;
		url.resize(regs.rsi);
		inst.machine().copy_from_guest(url.data(), regs.rdi, regs.rsi);
		printf("Curl: %s\n", url.c_str());

		struct opresult {
			long status;
			uint64_t content_length;
			uint64_t content_addr;
			uint64_t ct_length;
			char     ctype[0];
		};
		opresult opres;
		inst.machine().copy_from_guest(&opres, regs.rdx, sizeof(opresult));
		opres.content_addr = inst.machine().mmap();

		struct writeop {
			tinykvm::Machine& machine;
			uint64_t dst;
		};
		writeop op {
			.machine = inst.machine(),
			.dst   = inst.machine().mmap(),
		};

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8); /* Seconds */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (write_callback)
		[] (char *ptr, size_t size, size_t nmemb, void *poop) -> size_t {
			auto& woop = *(writeop *)poop;
			const size_t total = size * nmemb;
			woop.machine.copy_to_guest(woop.dst, ptr, total);
			woop.dst += total;
			return total;
		});
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &op);
		CURLcode res = curl_easy_perform(curl);
		if (res == 0) {
			/* Calculate content length */
			opres.content_length = op.dst - opres.content_addr;
			/* Adjust and set new mmap base */
			op.dst += 0xFFF; op.dst &= ~0xFFFL;
			inst.machine().mmap() = op.dst;
			/* Get response status and Content-Type */
			res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &opres.status);
			const char* ctype = nullptr;
			res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ctype);
			if (res == 0 && ctype != nullptr) {
				const size_t ctlen = std::min(strlen(ctype)+1, (size_t)opres.ct_length);
				opres.ct_length = ctlen;
				inst.machine().copy_to_guest(regs.rdx + offsetof(opresult, ctype), ctype, ctlen);
			} else {
				opres.ct_length = 0;
			}
			inst.machine().copy_to_guest(regs.rdx, &opres, sizeof(opres));
			regs.rax = 0;
		} else {
			regs.rax = -1;
		}
		curl_easy_cleanup(curl);
		inst.machine().set_registers(regs);
	});
}

}
