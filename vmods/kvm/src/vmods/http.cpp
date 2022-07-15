#include "../tenant.hpp"
#include "../machine_instance.hpp"
#include "../varnish.hpp"
#include <cassert>
#include <cstring>
#include <array>
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
	tinykvm::Machine* machine;
	uint64_t src;
	size_t   bytes;
};

void initialize_curl(VRT_CTX, VCL_PRIV task)
{
	(void) ctx;
	/* CURL is already linked to libvmod_kvm. */
	TenantConfig::set_dynamic_call(task, "curl.fetch",
	[=] (MachineInstance& inst, tinykvm::vCPU& vcpu)
	{
		auto regs = vcpu.registers();
		/**
		 * rdi = URL
		 * rsi = URL length
		 * rdx  = result buffer
		**/
		const uint64_t g_buffer = regs.rdx;
		const int CONN_TIMEOUT = 5;
		const int READ_TIMEOUT = 8;
		constexpr bool VERBOSE_CURL = false;

		/* URL */
		std::string url;
		url.resize(regs.rsi);
		vcpu.machine().copy_from_guest(url.data(), regs.rdi, regs.rsi);

		constexpr size_t CONTENT_TYPE_LEN = 128;
		constexpr size_t CURL_FIELDS_NUM = 8;
		struct opfields {
			uint64_t addr[CURL_FIELDS_NUM];
			uint16_t len[CURL_FIELDS_NUM];
		};
		struct opresult {
			long status;
			uint64_t post_buflen;
			uint64_t post_addr;
			uint64_t fields;
			uint64_t content_addr;
			uint32_t content_length;
			uint32_t ct_length;
			char     ctype[CONTENT_TYPE_LEN];
		};
		opresult opres;
		vcpu.machine().copy_from_guest(&opres, g_buffer, sizeof(opresult));

		// Retrieve request header fields into string vector
		std::array<std::string, CURL_FIELDS_NUM> fields;
		if (opres.fields != 0x0) {
			opfields of;
			vcpu.machine().copy_from_guest(&of, opres.fields, sizeof(of));
			/* Iterate through all the request fields. */
			for (size_t i = 0; i < CURL_FIELDS_NUM; i++) {
				if (of.addr[i] != 0x0 && of.len[i] != 0x0) {
					// Add to our temporary request field vector
					fields[i].resize(of.len[i]);
					vcpu.machine().copy_from_guest(fields[i].data(), of.addr[i], of.len[i]);
				}
			}
		}

		// XXX: Fixme, mmap is basic/unreliable
		opres.content_addr = vcpu.machine().mmap();
		const bool is_post = (opres.post_addr != 0x0 && opres.post_buflen != 0x0);

		if constexpr (VERBOSE_CURL) {
		printf("Curl: %s (%s)\n", url.c_str(), is_post ? "POST" : "GET");
		}

		writeop op {
			.machine = vcpu.machine(),
			.dst   = vcpu.machine().mmap(),
		};

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONN_TIMEOUT);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, READ_TIMEOUT); /* Seconds */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (write_callback)
		[] (char *ptr, size_t size, size_t nmemb, void *poop) -> size_t {
			auto& woop = *(writeop *)poop;
			const size_t total = size * nmemb;
			try {
				woop.machine.copy_to_guest(woop.dst, ptr, total);
				woop.dst += total;
				return total;
			} catch (...) {
				return 0;
			}
		});
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &op);

		/* Request header fields. */
		struct curl_slist *req_list = NULL;
		if (opres.fields != 0x0) {
			for (const auto& field : fields) {
				if (!field.empty())
					req_list = curl_slist_append(req_list, field.c_str());
			}
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_list);
		}

		/* Optional POST: We need a valid buffer and size. */
		struct curl_slist *post_list = NULL;
		readop rop;
		if (is_post)
		{
			curl_easy_setopt(curl, CURLOPT_POST, 1);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, opres.post_buflen);
			rop = readop {
				.machine = &inst.machine(),
				.src = opres.post_addr,
				.bytes = opres.post_buflen,
			};
			curl_easy_setopt(curl, CURLOPT_READFUNCTION, (read_callback)
			[] (char *ptr, size_t size, size_t nmemb, void *poop) -> size_t {
				auto& rop = *(readop *)poop;
				const size_t total = std::min(rop.bytes, size * nmemb);
				try {
					rop.machine->copy_from_guest(ptr, rop.src, total);
					rop.src += total;
					return total;
				} catch (...) {
					return 0;
				}
			});
			curl_easy_setopt(curl, CURLOPT_READDATA, &rop);

			/* Optional Content-Type for POST. */
			if (opres.ct_length != 0 && opres.ct_length < CONTENT_TYPE_LEN) {
				/* Copy Content-Type from guest opres into string. */
				std::string ct = "Content-Type: ";
				ct.resize(ct.size() + opres.ct_length);
				inst.machine().copy_from_guest(&ct[14], g_buffer + offsetof(opresult, ctype), opres.ct_length);

				post_list = curl_slist_append(post_list, ct.c_str());
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, post_list);
			}
		}

		CURLcode res = curl_easy_perform(curl);
		if (res == 0) {
			/* Calculate content length */
			opres.content_length = op.dst - opres.content_addr;
			/* Adjust and set new mmap base */
			op.dst += 0xFFF; op.dst &= ~0xFFFL;
			vcpu.machine().mmap() = op.dst;
			/* Get response status and Content-Type */
			res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &opres.status);
			const char* ctype = nullptr;
			res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ctype);
			/* We have an expectation of at least CONTENT_TYPE_LEN bytes available for
			   writing back Content-Type, directly into opres structure. */
			if (res == 0 && ctype != nullptr) {
				const size_t ctlen = std::min(strlen(ctype)+1, CONTENT_TYPE_LEN);
				opres.ct_length = ctlen;
				std::memcpy(opres.ctype, ctype, ctlen);
			}
			else
			{
				opres.ct_length = 0;
			}
			vcpu.machine().copy_to_guest(g_buffer, &opres, sizeof(opres));
			regs.rax = 0;
		} else {
			if constexpr (VERBOSE_CURL) {
				printf("cURL error: %d\n", res);
			}
			regs.rax = -res;
		}
		curl_easy_cleanup(curl);
		curl_slist_free_all(req_list);
		curl_slist_free_all(post_list);
		vcpu.set_registers(regs);
	});
}

} // kvm
