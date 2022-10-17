/**
 * @file tenant_instance.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Live tenant configuration and programs.
 * @version 0.1
 * @date 2022-07-23
 * 
 * Contains the current program and debug-program for a tenant.
 * Both programs can be hot-swapped during execution at any time,
 * and atomic ref-counting is used to make sure that every request
 * keeps it alive until completion.
 * 
 * Also contains tenant configuration, which includes things like
 * timeouts, memory limits and other settings.
 * 
**/
#include "tenant_instance.hpp"

#include "common_defs.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <sys/stat.h>
#include <unistd.h>
extern "C" void VTIM_format(double, char[32]);

namespace kvm {
	extern std::vector<uint8_t> file_loader(const std::string&);

TenantInstance::TenantInstance(const TenantConfig& conf)
	: config{conf}
{
	static bool init = false;
	if (!init) {
		init = true;
		MachineInstance::kvm_initialize();
	}
}
TenantInstance::TenantInstance(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	static bool init = false;
	if (!init) {
		init = true;
		MachineInstance::kvm_initialize();
	}

	this->begin_initialize(ctx);
}

void TenantInstance::begin_initialize(VRT_CTX)
{
	/* Prevent initializing many times, with a warning. */
	if (this->m_started_init) {
		VSL(SLT_VCL_Error, 0,
			"Program '%s' has already been initialized.",
			config.name.c_str());
		fprintf(stderr,
			"Program '%s' has already been initialized.",
			config.name.c_str());
		return;
	}
	this->m_started_init = true;

	bool filename_accessible = false;
	std::string filename_mtime = "";

	if (!config.filename.empty()) {
		if (access(config.filename.c_str(), R_OK) == 0)
		{
			filename_accessible = true;
			struct stat st;
			if (stat(config.filename.c_str(), &st) == 0) {
				char buf[32];
				VTIM_format(st.st_mtim.tv_sec, buf);
				filename_mtime = "If-Modified-Since: " + std::string(buf);
			}
		}
	}

	/* 1. If program has an URI, use cURL. */
	if (!config.uri.empty())
	{
		/* Load the program from cURL fetch. */
		try {
			this->program = std::make_shared<ProgramInstance> (
				config.uri, std::move(filename_mtime), ctx, this);
		} catch (const std::exception& e) {
			/* TODO: Retry with file loader here from local filesyste, if
			   the cURL fetch does not succeed. */
			this->handle_exception(config, e);
		}
		return;
	}
	/* 2. If filename is empty, do nothing (with warning in the logs). */
	else if (config.filename.empty()) {
		VSL(SLT_VCL_Error, 0,
			"No filename specified for '%s'. Send new program.",
			config.name.c_str());
		fprintf(stderr,
			"No filename specified for '%s'. Send new program.\n",
			config.name.c_str());
		return;
	}
	/* 3. Check program was in-accessible on local filesystem. */
	else if (!filename_accessible) {
		/* It is *NOT* accessible. */
		VSL(SLT_VCL_Error, 0,
			"Missing program or invalid path for '%s'. Send new program.",
			config.name.c_str());
		fprintf(stderr,
			"Missing program or invalid path for '%s'. Send new program.\n",
			config.name.c_str());
		return;
	}

	/* 4. Load the program from filesystem now. */
	try {
		auto elf = file_loader(config.filename);
		this->program =
			std::make_shared<ProgramInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		this->handle_exception(config, e);
	}
}
void TenantInstance::begin_async_initialize(const vrt_ctx *ctx)
{
	/* Block other requests from trying to initialize. */
	std::scoped_lock lock(this->mtx_running_init);

	if (!this->m_started_init) {
		this->begin_initialize(ctx);
	}
}
bool TenantInstance::wait_guarded_initialize(const vrt_ctx *ctx, std::shared_ptr<ProgramInstance>& prog)
{
	begin_async_initialize(ctx);

	/* This may take some time, as it is blocking, but this will allow the
		request to proceed.
		XXX: Verify that there are no forever-waiting events here. */
	this->wait_for_initialization();

	prog = std::atomic_load(&this->program);
	return prog != nullptr;
}

void TenantInstance::handle_exception(const TenantConfig& conf, const std::exception& e)
{
	VSL(SLT_Error, 0,
		"Exception when creating machine '%s': %s",
		conf.name.c_str(), e.what());
	fprintf(stderr,
		"Exception when creating machine '%s': %s\n",
		conf.name.c_str(), e.what());
	this->program = nullptr;
}

long TenantInstance::wait_for_initialization()
{
	auto prog = this->program;
	if (prog != nullptr)
		return prog->wait_for_initialization();
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
		try
		{
			std::shared_ptr<ProgramInstance> prog;
			if (LIKELY(!debug))
				prog = std::atomic_load(&this->program);
			else
				prog = std::atomic_load(&this->debug_program);
			// First-time tenants could have no program loaded
			if (UNLIKELY(prog == nullptr)) {
				// Attempt to load the program (if it was never attempted)
				// XXX: But not for debug programs (NOT IMPLEMENTED YET).
				if (debug || this->wait_guarded_initialize(ctx, prog) == false) {
					VRT_fail(ctx, "vmreserve: Missing program for %s. Not uploaded?",
						config.name.c_str());
					return nullptr;
				}
				// On success, prog is now loaded with the new program.
				// XXX: Assert on prog
			}
			// Avoid reservation while still initializing. Wait for lock.
			// Returns false if the main_vm failed to initialize.
			if (UNLIKELY(!prog->wait_for_main_vm())) {
				return nullptr;
			}

			// Reserve a machine through blocking queue.
			// May throw if dequeue from the queue times out.
			Reservation resv = prog->reserve_vm(ctx, this, std::move(prog));
			// prog is nullptr after this ^

			priv_task->priv = resv.slot;
			priv_task->len  = KVM_PROGRAM_MAGIC;
			priv_task->free = resv.free;
		} catch (std::exception& e) {
			// It makes no sense to reserve a VM without a request w/VSL
			VSLb(ctx->vsl, SLT_Error,
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

MachineInstance* TenantInstance::tlsreserve(const vrt_ctx* ctx, bool debug)
{
	std::shared_ptr<ProgramInstance> prog;
	if (LIKELY(!debug))
		prog = std::atomic_load(&this->program);
	else
		prog = std::atomic_load(&this->debug_program);
	// First-time tenants could have no program loaded
	if (UNLIKELY(prog == nullptr))
	{
		// Attempt to load the program (if it was never attempted)
		// XXX: But not for debug programs (NOT IMPLEMENTED YET).
		if (debug || this->wait_guarded_initialize(ctx, prog) == false)
		{
			VRT_fail(ctx, "vmreserve: Missing program for %s. Not uploaded?",
					 config.name.c_str());
			return nullptr;
		}
		// On success, prog is now loaded with the new program.
		// XXX: Assert on prog
	}
	// Avoid reservation while still initializing. Wait for lock.
	// Returns false if the main_vm failed to initialize.
	if (UNLIKELY(!prog->wait_for_main_vm()))
	{
		return nullptr;
	}

	// Reserve a machine through blocking queue.
	// May throw if dequeue from the queue times out.
	return prog->tls_reserve_vm(ctx, this, std::move(prog));
	// prog is nullptr after this ^
}

uint64_t TenantInstance::lookup(const char* name) const {
	auto inst = program;
	if (inst != nullptr)
		return inst->lookup(name);
	return 0x0;
}

void TenantInstance::dynamic_call(uint32_t hash, tinykvm::vCPU& cpu, MachineInstance& machine) const
{
	const auto& dfm = config.dynamic_functions_ref;

	auto it = dfm.find(hash);
	if (it != dfm.end()) {
		it->second(machine, cpu);
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
			long res =
				old->live_update_call(ctx, old_ser_func, *inst, new_deser_func);
			VSLb(ctx->vsl, SLT_VCL_Log,
				 "Transferred %ld bytes", res);
		} else {
			VSLb(ctx->vsl, SLT_VCL_Log,
				"Live-update deserialization skipped (new program lacks restorer)");
		}
	} else {
		VSLb(ctx->vsl, SLT_VCL_Log,
			"Live-update skipped (old program lacks serializer)");
	}
}

void TenantInstance::commit_program_live(VRT_CTX,
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
			ctx, current, new_prog);
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
