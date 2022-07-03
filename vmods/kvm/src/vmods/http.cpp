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
typedef size_t (*read_callback)(char *buffer, size_t size, size_t nitems, void *);
typedef size_t (*write_callback)(char *, size_t, size_t, void *);
struct writeop {
	tinykvm::Machine& machine;
	uint64_t dst;
};
struct readop {
	tinykvm::Machine& machine;
	uint64_t src;
	size_t   bytes;
};

void initialize_curl(VRT_CTX, VCL_PRIV task)
{
	(void) ctx;
	/* CURL is already linked to libvmod_kvm. */
	TenantConfig::set_dynamic_call(task, "curl.fetch",
	[=] (MachineInstance& inst)
	{
		auto regs = inst.machine().registers();
		/**
		 * rdi = URL
		 * rsi = URL length
		 * rdx  = result buffer
		**/
		const uint64_t g_buffer = regs.rdx;
		const int TIMEOUT = 8;

		/* URL */
		std::string url;
		url.resize(regs.rsi);
		inst.machine().copy_from_guest(url.data(), regs.rdi, regs.rsi);

		struct opresult {
			long status;
			uint64_t post_buflen;
			uint64_t post_addr;
			uint64_t content_length;
			uint64_t content_addr;
			uint64_t ct_length;
			char     ctype[256];
		};
		opresult opres;
		inst.machine().copy_from_guest(&opres, g_buffer, sizeof(opresult));
		// XXX: Fixme, mmap is basic/unreliable
		opres.content_addr = inst.machine().mmap();
		const bool is_post = (opres.post_addr != 0x0 && opres.post_buflen != 0x0);

		printf("Curl: %s (%s)\n", url.c_str(), is_post ? "POST" : "GET");

		writeop op {
			.machine = inst.machine(),
			.dst   = inst.machine().mmap(),
		};

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT); /* Seconds */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (write_callback)
		[] (char *ptr, size_t size, size_t nmemb, void *poop) -> size_t {
			auto& woop = *(writeop *)poop;
			const size_t total = size * nmemb;
			woop.machine.copy_to_guest(woop.dst, ptr, total);
			woop.dst += total;
			return total;
		});
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &op);

		/* Optional POST: We need a valid buffer and size. */
		if (is_post)
		{
			curl_easy_setopt(curl, CURLOPT_POST, 1);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, opres.post_buflen);
			readop rop {
				.machine = inst.machine(),
				.src = opres.post_addr,
				.bytes = opres.post_buflen,
			};
			curl_easy_setopt(curl, CURLOPT_READFUNCTION, (read_callback)
			[] (char *ptr, size_t size, size_t nmemb, void *poop) -> size_t {
				auto& rop = *(readop *)poop;
				const size_t total = std::min(rop.bytes, size * nmemb);
				rop.machine.copy_from_guest(ptr, rop.src, total);
				rop.src += total;
				return total;
			});
			curl_easy_setopt(curl, CURLOPT_READDATA, &rop);

			/* Optional Content-Type for POST. */
			if (opres.ct_length != 0 && opres.ct_length < 256) {
				/* Copy Content-Type from guest opres into string. */
				std::string ct = "Content-Type: ";
				ct.resize(ct.size() + opres.ct_length);
				inst.machine().copy_from_guest(&ct[14], g_buffer + offsetof(opresult, ctype), opres.ct_length);

				struct curl_slist *list = NULL;
				list = curl_slist_append(list, ct.c_str());
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
			}
		}

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
				std::memcpy(opres.ctype, ctype, ctlen);
			}
			else
			{
				opres.ct_length = 0;
			}
			inst.machine().copy_to_guest(g_buffer, &opres, sizeof(opres));
			regs.rax = 0;
		} else {
			regs.rax = -1;
		}
		curl_easy_cleanup(curl);
		inst.machine().set_registers(regs);
	});
}

} // kvm
