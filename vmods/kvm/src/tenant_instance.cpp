#include "tenant_instance.hpp"
#include "varnish.hpp"

namespace kvm {
	std::vector<uint8_t> file_loader(const std::string& file);

TenantInstance::TenantInstance(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	init_vmods(ctx);
	try {
		auto elf = file_loader(conf.filename);
		this->program =
			std::make_shared<ProgramInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating machine '%s': %s",
			conf.name.c_str(), e.what());
		this->program = nullptr;
	}
}

void TenantInstance::init_vmods(VRT_CTX)
{
	/* TODO: implement Goto as dynamic call */
}

MachineInstance* TenantInstance::vmfork(const vrt_ctx* ctx, bool debug)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (!priv_task->priv)
	{
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		std::shared_ptr<ProgramInstance> prog;
		if (!debug)
			prog = this->program;
		else
			prog = this->debug_program;
		/* First-time tenants could have no program */
		if (UNLIKELY(prog == nullptr))
			return nullptr;
		/* Allocate Script on workspace, and construct it in-place */
		auto* inst = (MachineInstance*) WS_Alloc(ctx->ws, sizeof(MachineInstance));
		if (UNLIKELY(inst == nullptr)) {
			VRT_fail(ctx, "vmfork: Out of workspace");
			return nullptr;
		}

		try {
			new (inst) MachineInstance{prog->script, ctx, this, *prog};
			/* This creates a self-reference, which ensures that open
			   Script instances will keep the machine instance alive. */
			inst->assign_instance(prog);

		} catch (std::exception& e) {
			VRT_fail(ctx,
				"VM '%s' exception: %s", inst->name().c_str(), e.what());
			return nullptr;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		timing_constr.add(t0, t1);
	#endif

		priv_task->priv = inst;
		priv_task->len  = KVM_PROGRAM_MAGIC;
		priv_task->free = [] (void* inst) {
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t2);
		#endif
			((MachineInstance*) inst)->~MachineInstance(); /* call destructor */
		#ifdef ENABLE_TIMING
			TIMING_LOCATION(t3);
			timing_destr.add(t2, t3);
		#endif
		};
	}
	return (MachineInstance*) priv_task->priv;
}

/* XXX: add compile-time CRC32 */
uint32_t crc32(const char *, size_t) {
	return 0;
}

void TenantInstance::set_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	printf("*** DynCall %s is registered as 0x%X\n", name.c_str(), hash);
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		throw std::runtime_error("set_dynamic_call: Hash collision for " + name);
	}
	m_dynamic_functions.emplace(hash, std::move(handler));
}
void TenantInstance::reset_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	m_dynamic_functions.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(name, std::move(handler));
	}
}
void TenantInstance::set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(pair.first, std::move(pair.second));
	}
}
void TenantInstance::dynamic_call(uint32_t hash, MachineInstance& machine) const
{
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		it->second(machine);
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

} // kvm
