#include "sandbox.hpp"
#include "varnish.hpp"
extern "C" {
#  include "update_result.h"
}
static bool file_writer(const std::string& file, const std::vector<uint8_t>&);
static const size_t TOO_SMALL = 3; // vmcalls that can be skipped

extern SandboxTenant* create_temporary_tenant(const SandboxTenant*, const std::string&);
extern void delete_temporary_tenant(const SandboxTenant*);

constexpr update_result
static_result(const char* text) {
	return { text, __builtin_strlen(text), nullptr };
}
static update_result
dynamic_result(const char* text) {
	return { strdup(text), __builtin_strlen(text),
		[] (update_result* res) { free((void*) res->output); } };
}

extern "C"
struct update_result
riscv_update(VRT_CTX, SandboxTenant* vrm, struct update_params *params)
{
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
			std::move(binary), nullptr, vrm, params->is_debug);
		const auto& live_binary = inst->binary;

		/* Check if self-test is present */
		if (const auto selftest = inst->lookup("selftest");
			selftest != 0x0)
		{
			/* Create a temporary tenant */
			auto* temp = create_temporary_tenant(
				vrm, vrm->config.name + "_temporary");
			try {
				/* Create machine with VRT context */
				temp->machine = std::make_shared<MachineInstance>(live_binary, ctx, temp);
				/* Run the tenants self-test function manually.
				   We want to propagate any exceptions to the client. */
				auto& machine = temp->machine->script.machine();
				machine.vmcall<2'000'000, true>(selftest);
			} catch (...) {
				delete_temporary_tenant(temp);
				throw; /* Re-throw */
			}
			delete_temporary_tenant(temp);
		} else {
			VSLb(ctx->vsl, SLT_Debug, "Self-test was skipped (not found)");
		}

		std::shared_ptr<MachineInstance> old;
		if (!params->is_debug)
		{
			/* Decrements reference when it goes out of scope.
			   We need the *new* instance alive for access to the binary
			   when writing it to disk. Don't *move*. See below. */
			old = std::atomic_exchange(&vrm->machine, inst);

		} else {
			/* Live-debugging temporary tenant */
			old = std::atomic_exchange(&vrm->debug_machine, inst);
		}

		if (old != nullptr) {
			const auto luaddr = old->lookup("on_live_update");
			if (luaddr != 0x0) {
				const auto resaddr = inst->lookup("on_resume_update");
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

extern "C"
Script* riscv_fork(VRT_CTX, const char* tenant, int debug)
{
	extern SandboxTenant* tenant_find(VRT_CTX, const char* name);
	auto* vrm = tenant_find(ctx, tenant);
	if (UNLIKELY(vrm == nullptr))
		return nullptr;

	auto* script = vrm->vmfork(ctx, debug);
	if (UNLIKELY(script == nullptr))
		return nullptr;

	return script;
}

extern "C"
uint64_t riscv_resolve_name(struct SandboxTenant* vrm, const char* symb)
{
	return vrm->lookup(symb);
}


inline Script* get_machine(VRT_CTX, const void* key)
{
	auto* priv_task = VRT_priv_task(ctx, key);
	//printf("priv_task: ctx=%p bo=%p key=%p task=%p\n", ctx, ctx->bo, key, priv_task);
	if (priv_task->priv && priv_task->len == SCRIPT_MAGIC)
		return (Script*) priv_task->priv;
	return nullptr;
}
inline Script* get_machine(VRT_CTX)
{
	return get_machine(ctx, ctx);
}

extern "C"
const struct SandboxTenant* riscv_current_machine(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script) {
		return script->vrm();
	}
	return nullptr;
}

extern "C"
long riscv_current_call(VRT_CTX, const char* func)
{
	auto* script = get_machine(ctx);
	if (script) {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		const auto addr = script->vrm()->lookup(func);
		if (UNLIKELY(addr == 0)) {
			VSLb(ctx->vsl, SLT_Error,
				"VM call '%s' failed: The function is missing", func);
			return -1;
		}
		int ret = script->call(addr);
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return ret;
	}
	return -1;
}
extern "C"
long riscv_current_call_idx(VRT_CTX, vcall_info info)
{
	auto* script = get_machine(ctx);
	if (script) {
		if (info.idx >= 0 && info.idx < script->instance().sym_vector.size())
		{
			auto& entry = script->instance().sym_vector[info.idx];
			if (UNLIKELY(entry.addr == 0)) {
				VSLb(ctx->vsl, SLT_Error,
					"VM call '%s' failed: The function at index %d is not availble",
					entry.func, info.idx);
				return -1;
			}
			if (UNLIKELY(entry.size <= TOO_SMALL)) {
				VSLb(ctx->vsl, SLT_Debug, "VM call '%s' is being skipped.",
					entry.func);
				//printf("Callsite: %s -> %zu\n", entry.func, entry.size);
				return 0;
			}
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t1);
		#endif
			int ret = script->call(entry.addr, (int) info.arg1, (int) info.arg2);
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return ret;
		}
		VRT_fail(ctx, "VM call failed (invalid index given: %d)", info.idx);
		return -1;
	}
	VRT_fail(ctx, "current_call_idx() failed (no running machine)");
	return -1;
}
extern "C"
long riscv_current_resume(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script) {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
	#endif
		long ret = script->resume(script->max_instructions());
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t2);
		printf("Time spent in resume(): %ld ns\n", nanodiff(t1, t2));
	#endif
		return ret;
	}
	return -1;
}

extern "C"
const char* riscv_current_name(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->name().c_str();
	return nullptr;
}
extern "C"
const char* riscv_current_group(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->group().c_str();
	return nullptr;
}
extern "C"
const char* riscv_current_result(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->want_result();
	return nullptr;
}
extern "C"
long riscv_current_result_value(VRT_CTX, size_t idx)
{
	auto* script = get_machine(ctx);
	if (script && idx < Script::RESULTS_MAX)
		return script->want_values().at(idx);
	return 503;
}
extern "C"
int  riscv_current_is_paused(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->is_paused();
	return 0;
}
extern "C"
int  riscv_current_apply_hash(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->apply_hash();
	return 0;
}

struct backend_buffer {
	const char* type;
	size_t      tsize;
	const char* data;
	size_t      size;
};
inline backend_buffer backend_error() {
	return backend_buffer {nullptr, 0, nullptr, 0};
}
inline const char* optional_copy(VRT_CTX, const riscv::Buffer& buffer)
{
	char* data = (char*) WS_Alloc(ctx->ws, buffer.size());
	// TODO: move to heap
	if (data == nullptr)
		throw std::runtime_error("Out of workspace");
	buffer.copy_to(data, buffer.size());
	return data;
}

extern "C"
struct backend_buffer riscv_backend_call(VRT_CTX, const void* key, long func, long farg)
{
	auto* script = get_machine(ctx, key);
	if (script) {
		auto* old_ctx = script->ctx();
		try {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t1);
		#endif
			/* Use backend ctx which can write to beresp */
			script->set_ctx(ctx);
			/* Call the backend response function */
			script->machine().vmcall(func,
				(Script::gaddr_t) farg, (int) HDR_BEREQ, (int) HDR_BERESP);
			/* Restore old ctx for backend_response */
			script->set_ctx(old_ctx);

			/* Get content-type and data */
			const auto [type, data] = script->machine().sysargs<riscv::Buffer, riscv::Buffer> ();
			/* Return content-type, data, size */
			const backend_buffer result {
				.type = optional_copy(ctx, type),
				.tsize = type.size(),
				.data = optional_copy(ctx, data),
				.size = data.size()
			};
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in backend_call(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return result;
		} catch (const riscv::MachineException& e) {
			fprintf(stderr, "Backend VM exception: %s (data: 0x%lX)\n",
				e.what(), e.data());
			VSLb(ctx->vsl, SLT_Error,
				"Backend VM exception: %s (data: 0x%lX)\n",
				e.what(), e.data());
		} catch (const std::exception& e) {
			fprintf(stderr, "Backend VM exception: %s\n", e.what());
			VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		}
		script->set_ctx(old_ctx);
	}
	return backend_error();
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
