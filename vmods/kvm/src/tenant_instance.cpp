#include "tenant_instance.hpp"

#include "common_defs.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
static constexpr bool FAST_RESET_METHOD = true;

namespace kvm {
	extern std::vector<uint8_t> file_loader(const std::string&);
	extern void initialize_vmods(VRT_CTX);

TenantInstance::TenantInstance(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	static bool init = false;
	if (!init) {
		init = true;
		MachineInstance::kvm_initialize();
	}

	try {
		auto elf = file_loader(conf.filename);
		this->program =
			std::make_shared<ProgramInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating machine '%s': %s",
			conf.name.c_str(), e.what());
		fprintf(stderr,
			"Exception when creating machine '%s': %s\n",
			conf.name.c_str(), e.what());
		this->program = nullptr;
	}
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
		if (LIKELY(!debug))
			prog = this->program;
		else
			prog = this->debug_program;
		/* First-time tenants could have no program */
		if (UNLIKELY(prog == nullptr)) {
			VRT_fail(ctx, "vmfork: Missing program for %s. Not uploaded?",
				config.name.c_str());
			return nullptr;
		}
		try {
			inst_pair ip;
			if constexpr (FAST_RESET_METHOD) {
				/* Get free instance through concurrent queue */
				ip = prog->concurrent_fork(ctx, this, prog);
			} else {
				/* Create new instance on workspace by forking */
				ip = prog->workspace_fork(ctx, this, prog);
			}

			priv_task->priv = ip.inst;
			priv_task->len  = KVM_PROGRAM_MAGIC;
			priv_task->free = ip.free;
		} catch (std::exception& e) {
			VRT_fail(ctx,
				"VM '%s' exception: %s", config.name.c_str(), e.what());
			return nullptr;
		}
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t1);
		timing_constr.add(t0, t1);
	#endif
	}
	return (MachineInstance*) priv_task->priv;
}

uint64_t TenantInstance::lookup(const char* name) const {
	auto inst = program;
	if (inst != nullptr)
		return inst->lookup(name);
	return 0x0;
}

void TenantInstance::dynamic_call(uint32_t hash, MachineInstance& machine) const
{
	const auto& dfm = config.dynamic_functions_ref;

	auto it = dfm.find(hash);
	if (it != dfm.end()) {
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
