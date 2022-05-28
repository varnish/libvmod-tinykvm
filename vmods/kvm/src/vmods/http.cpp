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

namespace kvm {
typedef size_t (*write_callback)(char *, size_t, size_t, void *);

void initialize_curl(VRT_CTX, VCL_PRIV task)
{
	(void) ctx;
	/* CURL is already linked to libvmod_kvm. */
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

} // kvm
