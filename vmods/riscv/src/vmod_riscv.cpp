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


inline Script* get_machine(VRT_CTX)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (priv_task->priv && priv_task->len == SCRIPT_MAGIC)
		return (Script*) priv_task->priv;
	return nullptr;
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

struct backend_buffer {
	const char* type;
	const char* data;
	unsigned    size;
};
extern "C"
struct backend_buffer riscv_backend_call(VRT_CTX, struct vmod_riscv_machine* vrm, long func)
{
	auto program = vrm->machine;
	if (program == nullptr)
		return {nullptr, nullptr, 0};
	auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
	if (UNLIKELY(script == nullptr)) {
		VSLb(ctx->vsl, SLT_Error, "Out of workspace");
		return {nullptr, nullptr, 0};
	}
	/* Fork new machine */
	try {
		new (script) Script{program->script, ctx, vrm, *program};
	} catch (std::exception& e) {
		VSLb(ctx->vsl, SLT_Error, "VM fork exception: %s", e.what());
		return {nullptr, nullptr, 0};
	}
	/* Call the backend response function */
	try {
		script->machine().vmcall(func);
		/* Get content-type and data */
		const auto [type, data] = script->machine().sysargs<riscv::Buffer, riscv::Buffer> ();
		auto* mimebuf = (char*) WS_Alloc(ctx->ws, type.size());
		auto* databuf = (char*) WS_Alloc(ctx->ws, data.size());
		if (mimebuf && databuf)
		{
			/* Return content-type, data, size */
			backend_buffer result {type.to_buffer(mimebuf), data.to_buffer(databuf), (unsigned) data.size()};
			script->~Script(); /* call destructor */
			return result;
		}
	} catch (std::exception& e) {
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		printf("VM call exception: %s\n", e.what());
	}
	script->~Script(); /* call destructor */
	return {nullptr, nullptr, 0};
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
