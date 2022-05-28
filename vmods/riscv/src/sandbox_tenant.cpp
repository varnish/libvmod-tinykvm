#include "sandbox_tenant.hpp"
#include "varnish.hpp"
#include <libriscv/util/crc32.hpp>
using riscv::crc32;

namespace rvs {

//#define ENABLE_TIMING
#ifdef ENABLE_TIMING
#include "timing.hpp"
static Timing timing_constr {"constructor"};
static Timing timing_destr {"destructor"};
#endif

std::vector<uint8_t> file_loader(const std::string& file);

SandboxTenant::SandboxTenant(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	init_vmods(ctx);
	try {
		auto elf = file_loader(conf.filename);
		this->program =
			std::make_shared<MachineInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating machine '%s': %s",
			conf.name.c_str(), e.what());
		printf("Machine '%s' failed: %s\n",
			conf.name.c_str(), e.what());
		this->program = nullptr;
	}
}
void SandboxTenant::init()
{
	Script::init();
}

static inline void* get_priv_key(VRT_CTX) {
	if (ctx->req) return ctx->req;
	return ctx->bo;
}

Script* SandboxTenant::vmfork(VRT_CTX, bool debug)
{
	auto* priv_task = VRT_priv_task(ctx, get_priv_key(ctx));
	if (!priv_task->priv)
	{
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		std::shared_ptr<MachineInstance> prog;
		if (LIKELY(!debug))
			prog = this->program;
		else
			prog = this->debug_program;
		/* First-time tenants could have no program loaded */
		if (UNLIKELY(prog == nullptr))
			return nullptr;
		/* Allocate Script on workspace, and construct it in-place */
		uintptr_t saddr = (uintptr_t)WS_Alloc(ctx->ws, sizeof(Script) + 0x10);
		if (UNLIKELY(saddr == 0x0)) {
			VRT_fail(ctx, "Out of workspace");
			return nullptr;
		}
		// Make sure Script* is 16-byte aligned
		saddr = (saddr + 0xF) & ~(uintptr_t)0xF;
		auto* script = (Script*) saddr;

		try {
			new (script) Script{prog->script, ctx, this, *prog};
			/* This creates a self-reference, which ensures that open
			   Script instances will keep the machine instance alive. */
			script->assign_instance(prog);

		} catch (std::exception& e) {
			VRT_fail(ctx,
				"VM '%s' exception: %s", script->name().c_str(), e.what());
			return nullptr;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		timing_constr.add(t0, t1);
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
			timing_destr.add(t2, t3);
		#endif
		};
	}
	return (Script*) priv_task->priv;
}

void SandboxTenant::set_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	printf("*** DynCall %s is registered as 0x%X\n", name.c_str(), hash);
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		throw std::runtime_error("set_dynamic_call: Hash collision for " + name);
	}
	m_dynamic_functions.emplace(hash, std::move(handler));
}
void SandboxTenant::reset_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	m_dynamic_functions.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(name, std::move(handler));
	}
}
void SandboxTenant::set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(pair.first, std::move(pair.second));
	}
}
void SandboxTenant::dynamic_call(uint32_t hash, Script& script) const
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

} // rvs
