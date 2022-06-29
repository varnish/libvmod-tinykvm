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

	/* Check if the tenant has a program. */
	if (conf.filename.empty()) {
		VSL(SLT_Error, 0,
			"No filename specified for '%s'. Send new program.",
			conf.name.c_str());
		fprintf(stderr,
			"No filename specified for '%s'. Send new program.\n",
			conf.name.c_str());
		return;
	}
	/* Check if we can read the program filename. */
	else if (access(conf.filename.c_str(), R_OK)) {
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
long TenantInstance::wait_for_initialization()
{
	if (program != nullptr)
		return program->wait_for_initialization();
	return 0;
}

VMPoolItem* TenantInstance::vmreserve(const vrt_ctx* ctx, bool debug)
{
	// Priv-task have request lifetime and is a Varnish feature.
	// The key identifies any existing priv_task objects, which allows
	// us to reserve the same machine during multiple VCL stages.
	// The free callback is called at the end of a request
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
			prog = std::atomic_load(&this->program);
		else
			prog = std::atomic_load(&this->debug_program);
		// First-time tenants could have no program loaded
		if (UNLIKELY(prog == nullptr)) {
			VRT_fail(ctx, "vmreserve: Missing program for %s. Not uploaded?",
				config.name.c_str());
			return nullptr;
		}
		try {
			// Reserve a machine through blocking queue.
			Reservation resv = prog->reserve_vm(ctx, this, prog);

			priv_task->priv = resv.slot;
			priv_task->len  = KVM_PROGRAM_MAGIC;
			priv_task->free = resv.free;
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
	return (VMPoolItem*) priv_task->priv;
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
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) throw std::runtime_error("Could not open file: " + filename);

    fseek(f, 0, SEEK_END);
	const size_t size = ftell(f);
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
	auto old_ser_func =
		old->entry_at(ProgramEntryIndex::LIVEUPD_SERIALIZE);
	if (old_ser_func != 0x0)
	{
		auto new_deser_func =
			inst->entry_at(ProgramEntryIndex::LIVEUPD_DESERIALIZE);
		if (new_deser_func != 0x0)
		{
			VSLb(ctx->vsl, SLT_VCL_Log,
				"Live-update serialization will be performed");
			old->live_update_call(ctx, old_ser_func, *inst, new_deser_func);
		} else {
			VSLb(ctx->vsl, SLT_VCL_Log,
				"Live-update deserialization skipped (new program lacks restorer)");
		}
	} else {
		VSLb(ctx->vsl, SLT_VCL_Log,
			"Live-update skipped (old program lacks serializer)");
	}
}

void TenantInstance::commit_program_live(
	std::shared_ptr<ProgramInstance>& new_prog) const
{
	std::shared_ptr<ProgramInstance> current;
	/* Make a reference to the current program, keeping it alive */
	if (!new_prog->main_vm->is_debug()) {
		current = std::atomic_load(&this->program);
	} else {
		current = std::atomic_load(&this->debug_program);
	}

	if (current != nullptr) {
		TenantInstance::serialize_storage_state(
			new_prog->main_vm->ctx(), current, new_prog);
	}

	/* Swap out old program with new program. */
	if (!new_prog->main_vm->is_debug())
	{
		std::atomic_exchange(&this->program, new_prog);
	} else {
		std::atomic_exchange(&this->debug_program, new_prog);
	}
}

} // kvm
