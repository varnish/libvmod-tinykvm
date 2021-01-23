#include "sandbox.hpp"
#include "varnish.hpp"
#include "crc32.hpp"

//#define ENABLE_TIMING
#define TIMING_LOCATION(x) \
	asm("" ::: "memory"); \
	auto x = time_now();  \
	asm("" ::: "memory");


inline timespec time_now();
inline long nanodiff(timespec start_time, timespec end_time);
std::vector<uint8_t> file_loader(const std::string& file);

vmod_riscv_machine::vmod_riscv_machine(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	try {
		auto elf = file_loader(conf.filename);
		this->machine =
			std::make_shared<MachineInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating machine '%s': %s",
			conf.name.c_str(), e.what());
		machine = nullptr;
	}
}

Script* vmod_riscv_machine::vmfork(VRT_CTX)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (!priv_task->priv)
	{
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		auto program = this->machine;
		/* First-time tenants could have no program */
		if (UNLIKELY(program == nullptr))
			return nullptr;
		/* Allocate Script on workspace, and construct it in-place */
		auto* script = (Script*) WS_Alloc(ctx->ws, sizeof(Script));
		if (UNLIKELY(script == nullptr)) {
			VRT_fail(ctx, "Out of workspace");
			return nullptr;
		}

		try {
			new (script) Script{program->script, ctx, this, *program};
			script->assign_instance(program);

		} catch (std::exception& e) {
			VRT_fail(ctx,
				"VM '%s' exception: %s", script->name().c_str(), e.what());
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

int vmod_riscv_machine::forkcall(VRT_CTX, Script::gaddr_t addr)
{
	auto* script = this->vmfork(ctx);
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
	return ret;
}

void vmod_riscv_machine::set_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		throw std::runtime_error("set_dynamic_call: Hash collision for " + name);
	}
	m_dynamic_functions.emplace(hash, std::move(handler));
}
void vmod_riscv_machine::reset_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	m_dynamic_functions.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(name, std::move(handler));
	}
}
void vmod_riscv_machine::set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(pair.first, std::move(pair.second));
	}
}
void vmod_riscv_machine::dynamic_call(uint32_t hash, Script& script) const
{
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		it->second(script);
	} else {
		fprintf(stderr,
			"Unable to find dynamic function with hash: 0x%08x\n",
			hash);
		throw std::runtime_error("Unable to find dynamic function");
	}
}

#include <unistd.h>
std::vector<uint8_t> file_loader(const std::string& filename)
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
