#include "sandbox.hpp"
#include "varnish.hpp"
static bool file_writer(const std::string& file, const std::vector<uint8_t>&);
static const size_t TOO_SMALL = 3; // vmcalls that can be skipped

extern "C"
const char* riscv_update(struct vsl_log* vsl, vmod_riscv_machine* vrm, const uint8_t* data, size_t len)
{
	/* ELF loader will not be run for empty binary */
	if (UNLIKELY(data == nullptr || len == 0)) {
		return strdup("Empty file received");
	}
	try {
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		/* Note: CTX is NULL here */
		std::vector<uint8_t> binary {data, data + len};

		auto inst = std::make_shared<MachineInstance>(std::move(binary), nullptr, vrm);
		const auto& live_binary = inst->binary;
		/* Decrements reference when it goes out of scope.
		   We need the *new* instance alive for access to the binary
		   when writing it to disk. Don't *move*. See below. */
		auto old = std::atomic_exchange(&vrm->machine, inst);

	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent updating: %ld ns\n", nanodiff(t0, t1));
	#endif
		/* If we arrive here, the initialization was successful,
		   and we can proceed to store the program to disk. */
		bool ok = file_writer(vrm->config.filename, live_binary);
		if (!ok) {
			/* Writing the tenant program to file failed */
			char buffer[800];
			const int len = snprintf(buffer, sizeof(buffer),
				"Could not write '%s'", vrm->config.filename.c_str());
			VSLb(vsl, SLT_Error, "%.*s", len, buffer);
			return strdup(buffer);
		}
		return strdup("Update successful\n");
	} catch (const riscv::MachineException& e) {
		if (e.type() == riscv::OUT_OF_MEMORY) {
			/* Pass helpful explanation when OOM */
			return strdup("Binary ran out of memory during initialization");
		}
		/* Pass machine error back to the client */
		return strdup(e.what());
	} catch (const std::exception& e) {
		/* Pass unknown error back to the client */
		return strdup(e.what());
	}
}

extern "C"
Script* riscv_fork(VRT_CTX, const char* tenant)
{
	extern vmod_riscv_machine* tenant_find(VRT_CTX, const char* name);
	auto* vrm = tenant_find(ctx, tenant);
	if (UNLIKELY(vrm == nullptr))
		return nullptr;

	auto* script = vrm->vmfork(ctx);
	if (UNLIKELY(script == nullptr))
		return nullptr;

	return script;
}

extern "C"
uint64_t riscv_resolve_name(struct vmod_riscv_machine* vrm, const char* symb)
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
const struct vmod_riscv_machine* riscv_current_machine(VRT_CTX)
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
long riscv_current_call_idx(VRT_CTX, int index)
{
	auto* script = get_machine(ctx);
	if (script) {
		if (index >= 0 && index < script->instance().sym_vector.size())
		{
			auto& entry = script->instance().sym_vector[index];
			if (UNLIKELY(entry.addr == 0)) {
				VSLb(ctx->vsl, SLT_Error,
					"VM call '%s' failed: The function at index %d is not availble",
					entry.func, index);
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
			int ret = script->call(entry.addr);
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
			printf("Time spent in forkcall(): %ld ns\n", nanodiff(t1, t2));
		#endif
			return ret;
		}
		VRT_fail(ctx, "VM call failed (invalid index given: %d)", index);
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
long riscv_current_result_status(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->want_value();
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
	if (buffer.is_sequential())
		return buffer.c_str();
	else {
		/* This buffer is fragmented, so we need to copy
		   it piecewise into a memory allocation. */
		char* data = (char*) WS_Alloc(ctx->ws, buffer.size());
		if (data == nullptr)
			throw std::runtime_error("Out of workspace");
		buffer.copy_to(data, buffer.size());
		return data;
	}
}

extern "C"
struct backend_buffer riscv_backend_call(VRT_CTX, const void* key, long func)
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
			script->machine().vmcall(func);
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
		} catch (const std::exception& e) {
			VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
			printf("VM backend exception: %s\n", e.what());
			script->set_ctx(old_ctx);
		}
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
