#include "tenant_instance.hpp"

#include "common_defs.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <unistd.h>

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

	/* Check if we can read the program filename. */
	if (access(conf.filename.c_str(), R_OK)) {
		VSL(SLT_Error, 0,
			"Missing program or invalid path for '%s'. Send new program.",
			conf.name.c_str());
		fprintf(stderr,
			"Missing program or invalid path for '%s'. Send new program.\n",
			conf.name.c_str());
		return;
	}

	/* Load the program now. */
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
	struct vmod_priv* priv_task;
	if (ctx->req)
		priv_task = VRT_priv_task(ctx, ctx->req);
	else
		priv_task = VRT_priv_task(ctx, ctx->bo);
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
			/* Get free instance through concurrent queue */
			inst_pair ip = prog->concurrent_fork(ctx, this, prog);

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
	std::shared_ptr<ProgramInstance> current;
	/* Make a reference to the current program, keeping it alive */
	if (!new_prog->script->is_debug()) {
		current = this->program;
	} else {
		current = this->debug_program;
	}

	if (current != nullptr && !storage) {
		TenantInstance::serialize_storage_state(
			new_prog->script->ctx(), current, new_prog);
	}

	if (!new_prog->script->is_debug())
	{
		std::atomic_exchange(&this->program, new_prog);
	} else {
		std::atomic_exchange(&this->debug_program, new_prog);
	}
}

} // kvm
