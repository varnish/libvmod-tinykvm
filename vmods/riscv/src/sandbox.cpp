#include "sandbox.hpp"
#include "varnish.hpp"

inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
static std::vector<uint8_t> load_file(const std::string& filename);
static std::string riscv_default_program;
// functions used by all machines created during init, afterwards
std::vector<const char*> riscv_lookup_wishlist;
static const size_t TOO_SMALL = 8;
static const uint64_t MAX_MEMORY = 800 * 1024;
static const uint64_t MAX_HEAP   = 640 * 1024;

#define SCRIPT_MAGIC 0x83e59fa5

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");

extern "C"
void riscv_set_default(const char* filename)
{
	riscv_default_program = filename;
}

static std::vector<uint8_t> file_loader(const char* file)
{
	return load_file(file);
}

extern "C"
vmod_riscv_machine* riscv_create(const char* name, const char* group,
	const char* file, VRT_CTX, uint64_t insn)
{
	auto* vrm = (vmod_riscv_machine*) WS_Alloc(ctx->ws, sizeof(vmod_riscv_machine));
	if (UNLIKELY(vrm == nullptr))
		VRT_fail(ctx, "Out of workspace");

	try {
		new (vrm) vmod_riscv_machine(name, group, file_loader(file), ctx,
			/* Max instr: */ insn, MAX_MEMORY, MAX_HEAP);
		return vrm;
	} catch (const std::exception& e) {
		VRT_fail(ctx, "Exception when creating machine '%s': %s",
			name, e.what());
		return nullptr;
	}
}
extern "C"
const char* riscv_update(vmod_riscv_machine* vrm, const uint8_t* data, size_t len)
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
		auto inst = std::make_unique<MachineInstance>(std::move(binary), nullptr, vrm);
		vrm->machine.swap(inst);
		/* FIXME: not randomly drop old_instance */
		auto* decomissioned = inst.release();
		decomissioned->script.decomission();
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent updating: %ld ns\n", nanodiff(t0, t1));
	#endif
		/* TODO: delete when nobody uses it anymore */
		return strdup("Update successful");
	} catch (const std::exception& e) {
		/* Pass the actual error back to the client */
		return strdup(e.what());
	}
}

extern "C"
void riscv_prewarm(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	(void) ctx;
	const auto site = vrm->callsite(func);
	vrm->machine->sym_lookup.emplace(strdup(site.name.c_str()), site.address);
	vrm->machine->sym_vector.push_back({site.name.c_str(), site.address, site.size});
}

inline Script* vmfork(VRT_CTX, const vmod_riscv_machine* vrm)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (!priv_task->priv)
	{
		/* Allocate Script on workspace, and construct it in-place */
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
		if (UNLIKELY(script == nullptr)) {
			VRT_fail(ctx, "Out of workspace");
			return nullptr;
		}

		try {
			new (script) Script{vrm->script(), ctx, vrm};

		} catch (std::exception& e) {
			VRT_fail(ctx,
				"VM '%s' exception: %s", script->name(), e.what());
			return nullptr;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		printf("Time spent in initialization: %ld ns\n", nanodiff(t0, t1));
	#endif

		priv_task->priv = script;
		priv_task->len  = SCRIPT_MAGIC;
		priv_task->free = [] (void* script) {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
		#endif
			((Script*) script)->~Script(); /* call destructor */
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t3);
			printf("Time spent in destructor: %ld ns\n", nanodiff(t2, t3));
		#endif
		};
	}
	return (Script*) priv_task->priv;
}

extern "C"
int riscv_fork(VRT_CTX, vmod_riscv_machine* vrm)
{
	auto* script = vmfork(ctx, vrm);
	if (UNLIKELY(script == nullptr))
		return -1;

	return 0;
}

inline int forkcall(VRT_CTX, vmod_riscv_machine* vrm, Script::gaddr_t addr)
{
	auto* script = vmfork(ctx, vrm);
	if (UNLIKELY(script == nullptr))
		return -1;

	/* Call into the virtual machine */
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t2);
#endif
	int ret = script->call(addr);
#ifdef ENABLE_TIMING
	TIMING_LOCATION(t3);
	printf("Time spent in forkcall(): %ld ns\n", nanodiff(t2, t3));
#endif
	//printf("Machine pages: %zu\n", script->machine().memory.pages_total());
	return ret;
}

extern "C"
int riscv_forkcall(VRT_CTX, vmod_riscv_machine* vrm, const char* func)
{
	const auto addr = vrm->lookup(func);
	if (UNLIKELY(addr == 0)) {
		VSLb(ctx->vsl, SLT_Error,
			"VM call '%s' failed: The function is missing", func);
		return -1;
	}
	return forkcall(ctx, vrm, addr);
}
extern "C"
int riscv_forkcall_idx(VRT_CTX, vmod_riscv_machine* vrm, int index)
{
	if (index >= 0 && index < vrm->machine->sym_vector.size())
	{
		auto& entry = vrm->machine->sym_vector[index];
		if (UNLIKELY(entry.addr == 0)) {
			VSLb(ctx->vsl, SLT_Error,
				"VM call '%s' failed: The function is missing", entry.func);
			return -1;
		}
		if (UNLIKELY(entry.size <= TOO_SMALL)) {
			VSLb(ctx->vsl, SLT_Debug, "VM call '%s' is being skipped.",
				entry.func);
			//printf("Callsite: %s -> %zu\n", entry.func, entry.size);
			return 0;
		}

		return forkcall(ctx, vrm, entry.addr);
	}
	VRT_fail(ctx, "VM call failed (invalid call index given: %d)", index);
	return -1;
}

extern "C"
int riscv_free(vmod_riscv_machine* vrm)
{
	vrm->~vmod_riscv_machine();
	return 0;
}

extern "C"
void riscv_add_known(VRT_CTX, const char* func)
{
	(void) ctx;
	riscv_lookup_wishlist.push_back(func);
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
const char* riscv_current_name(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->name();
	return nullptr;
}
extern "C"
const char* riscv_current_group(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->name();
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
int riscv_current_result_status(VRT_CTX)
{
	auto* script = get_machine(ctx);
	if (script)
		return script->want_status();
	return 503;
}

struct backend_buffer {
	const char* type;
	const char* data;
	unsigned    size;
};
extern "C"
struct backend_buffer riscv_backend_call(VRT_CTX, struct vmod_riscv_machine* vrm, const char* func)
{
	const auto addr = vrm->lookup(func);
	if (UNLIKELY(addr == 0)) {
		VSLb(ctx->vsl, SLT_Error,
			"VM call '%s' failed: The function is missing", func);
		return {nullptr, nullptr, 0};
	}
	auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
	if (UNLIKELY(script == nullptr)) {
		VSLb(ctx->vsl, SLT_Error, "Out of workspace");
		return {nullptr, nullptr, 0};
	}
	/* Fork new machine */
	try {
		new (script) Script{vrm->script(), ctx, vrm};
	} catch (std::exception& e) {
		VSLb(ctx->vsl, SLT_Error, "VM fork exception: %s", e.what());
		return {nullptr, nullptr, 0};
	}
	/* Call the backend response function */
	try {
		script->machine().vmcall(addr);
		/* Get content-type and data */
		const auto [type, data] = script->machine().sysargs<std::string, riscv::Buffer> ();
		/* Return content-type, data, size */
		backend_buffer result {strdup(type.c_str()), data.to_buffer(), (unsigned) data.size()};
		script->~Script(); /* call destructor */
		return result;
	} catch (std::exception& e) {
		//VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
		printf("VM call exception: %s\n", e.what());
		script->~Script(); /* call destructor */
		return {nullptr, nullptr, 0};
	}
}

#include <unistd.h>
std::vector<uint8_t> load_file(const std::string& filename)
{
    size_t size = 0;
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) throw std::runtime_error("Could not open file: " + filename);

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> result(size);
    if (size != fread(result.data(), 1, size, f))
    {
        fclose(f);
        throw std::runtime_error("Error when reading from file: " + filename);
    }
    fclose(f);
    return result;
}

timespec time_now()
{
	timespec t;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t;
}
long nanodiff(timespec start_time, timespec end_time)
{
	assert(end_time.tv_sec == 0); /* We should never use seconds */
	return end_time.tv_nsec - start_time.tv_nsec;
}
