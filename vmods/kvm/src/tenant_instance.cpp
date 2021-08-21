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

void TenantInstance::serialize_storage_state(
	VRT_CTX,
	std::shared_ptr<ProgramInstance>& old,
	std::shared_ptr<ProgramInstance>& inst)
{
	const auto luaddr = old->lookup("on_live_update");
	if (luaddr != 0x0)
	{
		const auto resaddr = inst->lookup("on_resume_update");
		if (resaddr != 0x0)
		{
			VSLb(ctx->vsl, SLT_Debug,
				"Live-update serialization will be performed");
			old->live_update_call(luaddr, *inst, resaddr);
		} else {
			VSLb(ctx->vsl, SLT_Debug,
				"Live-update deserialization skipped (new binary lacks resume)");
		}
	} else {
		VSLb(ctx->vsl, SLT_Debug,
			"Live-update skipped (old binary lacks serializer)");
	}
}

void TenantInstance::commit_program_live(
	std::shared_ptr<ProgramInstance>& new_prog, bool storage) const
{
	std::shared_ptr<ProgramInstance> old;
	if (!new_prog->script.is_debug()) {
		old = this->program;
	} else {
		old = this->debug_program;
	}

	if (old != nullptr && !storage) {
		TenantInstance::serialize_storage_state(
			new_prog->script.ctx(), old, new_prog);
	}

	if (!new_prog->script.is_debug())
	{
		/* Decrements reference when it goes out of scope.
		   We need the *new* instance alive for access to the binary
		   when writing it to disk. Don't *move*. See below. */
		old = std::atomic_exchange(&this->program, new_prog);
	} else {
		/* Live-debugging temporary tenant */
		old = std::atomic_exchange(&this->debug_program, new_prog);
	}
}

} // kvm
