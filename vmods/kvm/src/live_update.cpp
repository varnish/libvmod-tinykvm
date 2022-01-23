#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <atomic>
#include <cstring>
extern "C" {
#  include "kvm_live_update.h"
}
static bool file_writer(const std::string& file, const std::vector<uint8_t>&);

constexpr update_result
static_result(const char* text, bool success) {
	return { text, __builtin_strlen(text), success, nullptr };
}
static update_result
dynamic_result(const char* text) {
	return { strdup(text), __builtin_strlen(text), false,
		[] (update_result* res) { free((void*) res->output); } };
}

extern "C"
struct update_result
kvm_live_update(VRT_CTX, kvm::TenantInstance* ten, struct update_params *params)
{
	using namespace kvm;
	/* ELF loader will not be run for empty binary */
	if (UNLIKELY(params->data == nullptr || params->len == 0)) {
		return static_result("Empty file received", false);
	}
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		/* Note: CTX is NULL here */
		std::vector<uint8_t> binary {params->data, params->data + params->len};

		/* If this throws an exception, we instantly fail the update */
		auto inst = std::make_shared<ProgramInstance>(
			std::move(binary), ctx, ten, params->is_debug);
		const auto& live_binary = inst->binary;

		/* Complex dance to replace the currently running program */
		ten->commit_program_live(inst, false);

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent updating: %ld ns\n", nanodiff(t0, t1));
	#endif
		if (!params->is_debug)
		{
			/* If we arrive here, the initialization was successful,
			   and we can proceed to store the program to disk. */
			bool ok = file_writer(ten->config.filename, live_binary);
			if (!ok) {
				/* Writing the tenant program to file failed */
				char buffer[800];
				const int len = snprintf(buffer, sizeof(buffer),
					"Could not write '%s'", ten->config.filename.c_str());
				VSLb(ctx->vsl, SLT_Error, "%.*s", len, buffer);
				return dynamic_result(buffer);
			}
		}
		return static_result("Update successful\n", true);
	} catch (const tinykvm::MachineException& e) {
		/* Pass machine error back to the client */
		char buffer[2048];
		snprintf(buffer, sizeof(buffer),
			"Machine exception: %s (data: 0x%lX)\n", e.what(), e.data());
		return dynamic_result(buffer);
	} catch (const std::exception& e) {
		/* Pass unknown error back to the client */
		return dynamic_result(e.what());
	}
}

bool file_writer(const std::string& filename, const std::vector<uint8_t>& binary)
{
    FILE* f = fopen(filename.c_str(), "wb");
    if (f == NULL)
		return false;

	const size_t n = fwrite(binary.data(), 1, binary.size(), f);
    fclose(f);
	return n == binary.size();
}
