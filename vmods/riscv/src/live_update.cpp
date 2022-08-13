#include "sandbox_tenant.hpp"
#include "varnish.hpp"
extern "C" {
#include "update_result.h"
}

namespace rvs {
constexpr update_result
static_result(const char* text) {
	return { text, __builtin_strlen(text), nullptr };
}
static update_result
dynamic_result(const char* text) {
	return { strdup(text), __builtin_strlen(text),
		[] (update_result* res) { free((void*) res->output); } };
}

static bool file_writer(const std::string& filename, const std::vector<uint8_t>& binary)
{
    FILE* f = fopen(filename.c_str(), "wb");
    if (f == NULL)
		return false;

	const size_t n = fwrite(binary.data(), 1, binary.size(), f);
    fclose(f);
	return n == binary.size();
}
} // rvs

extern "C"
struct update_result
riscv_update(VRT_CTX, rvs::SandboxTenant* vrm, struct update_params *params)
{
	using namespace rvs;

	/* ELF loader will not be run for empty binary */
	if (UNLIKELY(params->data == nullptr || params->len == 0)) {
		return static_result("Empty file received");
	}
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		/* Note: CTX is NULL here */
		std::vector<uint8_t> binary {params->data, params->data + params->len};

		/* If this throws an exception, we instantly fail the update */
		auto inst = std::make_shared<MachineInstance>(
			std::move(binary), ctx, vrm, params->is_debug);
		const auto& live_binary = inst->binary;

		std::shared_ptr<MachineInstance> old;
		if (!params->is_debug)
		{
			/* Decrements reference when it goes out of scope.
			   We need the *new* instance alive for access to the binary
			   when writing it to disk. Don't *move*. See below. */
			old = std::atomic_exchange(&vrm->program, inst);

		} else {
			/* Live-debugging temporary tenant */
			old = std::atomic_exchange(&vrm->debug_program, inst);
		}

		if (old != nullptr) { // 10 == on_live_update
			const auto luaddr = old->callback_entries.at(10);
			if (luaddr != 0x0) { // 11 == on_resume_update
				const auto resaddr = inst->callback_entries.at(11);
				if (resaddr != 0x0)
				{
				/* Serialize data in the old machine */
				auto& old_machine = old->storage;
				old_machine.call(luaddr);
				/* Get serialized data */
				auto [data_addr, data_len] =
					old_machine.machine().sysargs<Script::gaddr_t, unsigned> ();
				/* Allocate room for serialized data in new machine */
				auto& new_machine = inst->storage;
				auto dst_data = new_machine.guest_alloc(data_len);
				new_machine.machine().memory.memcpy(
					dst_data,
					old_machine.machine(), data_addr, data_len);
				/* Deserialize data in the new machine */
				new_machine.call(resaddr, dst_data, data_len);
				} else {
					VSLb(ctx->vsl, SLT_Debug,
						"Live-update deserialization skipped (new binary lacks resume)");
				}
			} else {
				VSLb(ctx->vsl, SLT_Debug,
					"Live-update skipped (old binary lacks serializer)");
			}
		} // old != null

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent updating: %ld ns\n", nanodiff(t0, t1));
	#endif
		if (!params->is_debug)
		{
			/* If we arrive here, the initialization was successful,
			   and we can proceed to store the program to disk. */
			bool ok = file_writer(vrm->config.filename, live_binary);
			if (!ok) {
				/* Writing the tenant program to file failed */
				char buffer[800];
				const int len = snprintf(buffer, sizeof(buffer),
					"Could not write '%s'", vrm->config.filename.c_str());
				VSLb(ctx->vsl, SLT_Error, "%.*s", len, buffer);
				return dynamic_result(buffer);
			}
		}
		return static_result("Update successful\n");
	} catch (const riscv::MachineException& e) {
		if (e.type() == riscv::OUT_OF_MEMORY) {
			/* Pass helpful explanation when OOM */
			return static_result("Program ran out of memory, update not applied");
		}
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
