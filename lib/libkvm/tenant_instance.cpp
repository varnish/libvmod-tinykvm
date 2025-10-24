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
//#include <openssl/sha.h>
#include <openssl/md5.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" void VTIM_format(double, char[32]);

namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);
extern std::string create_sha256_from_file(const std::string&);
extern std::string create_md5_from_file(const std::string&);

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

	const bool debug = false;
	this->begin_initialize(ctx, debug);
}

void TenantInstance::begin_initialize(VRT_CTX, bool debug)
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
	bool file_verified = false;

	if (!config.filename.empty()) {
		if (access(config.filename.c_str(), R_OK) == 0)
		{
			filename_accessible = true;
			if (!config.sha256.empty()) {
				// If SHA256 is specified, verify the file
				std::string hash_hex = create_sha256_from_file(config.filename);
				if (hash_hex == config.sha256) {
					file_verified = true;
				} else if (config.group.verbose) {
					printf("Local file '%s' exists but hash mismatch: %s vs %s, will re-download if URI is given\n",
						config.filename.c_str(), hash_hex.c_str(), config.sha256.c_str());
				}
			} else if (!config.md5.empty()) {
				// If MD5 is specified, verify the file
				std::string hash_hex = create_md5_from_file(config.filename);
				if (hash_hex == config.md5) {
					file_verified = true;
				} else if (config.group.verbose) {
					printf("Local file '%s' exists but MD5 mismatch: %s vs %s, will re-download if URI is given\n",
						config.filename.c_str(), hash_hex.c_str(), config.md5.c_str());
				}
			}
		}
	}

	/* 1. If program has an URI, use cURL (unless SHA256 is verified). */
	if (!config.uri.empty() && !file_verified)
	{
		/* Load the program from cURL fetch. */
		try {
			std::string filename_mtime;
			struct stat st;
			if (stat(config.filename.c_str(), &st) == 0) {
				char buf[32];
				VTIM_format(st.st_mtim.tv_sec, buf);
				filename_mtime = "If-Modified-Since: " + std::string(buf);
			}
			auto prog = std::make_shared<ProgramInstance> (
				config.uri, std::move(filename_mtime), ctx, this, debug);
			std::atomic_store(&this->program, std::move(prog));
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
		// file_loader(config.request_program_filename());
		BinaryStorage elf;
		BinaryStorage storage_elf;
		elf.set_binary(config.request_program_filename());
		/* Check for a storage program */
		if (access(config.storage_program_filename().c_str(), R_OK) == 0)
		{
			storage_elf.set_binary(config.storage_program_filename());
		}
		std::shared_ptr<ProgramInstance> prog =
			std::make_shared<ProgramInstance> (std::move(elf), std::move(storage_elf), ctx, this);
		std::atomic_store(&this->program, std::move(prog));

	} catch (const std::exception& e) {
		this->handle_exception(config, e);
	}
}
void TenantInstance::begin_async_initialize(const vrt_ctx *ctx, bool debug)
{
	/* Block other requests from trying to initialize. */
	std::scoped_lock lock(this->mtx_running_init);

	if (!this->m_started_init) {
		this->begin_initialize(ctx, debug);
	}
}
bool TenantInstance::wait_guarded_initialize(const vrt_ctx *ctx, std::shared_ptr<ProgramInstance>& prog)
{
	const bool debug = false;
	begin_async_initialize(ctx, debug);

	/* This may take some time, as it is blocking, but this will allow the
		request to proceed.
		XXX: Verify that there are no forever-waiting events here. */
	prog = this->wait_for_initialization();
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

std::shared_ptr<ProgramInstance> TenantInstance::wait_for_initialization()
{
	std::shared_ptr<ProgramInstance> prog = std::atomic_load(&this->program);
	if (prog != nullptr)
		prog->wait_for_initialization();
	return prog;
}

VMPoolItem* TenantInstance::vmreserve(const vrt_ctx* ctx, bool debug)
{
	// Priv-task have request lifetime and is a Varnish feature.
	// The key identifies any existing priv_task objects, which allows
	// us to reserve the same machine during multiple VCL stages.
	// The free callback is called at the end of a request
	struct vmod_priv* priv_task;
	// For requests, we use ctx->req in order to always have one
	// VM per tenant/program on the client-side.
	if (ctx->req) {
		priv_task = VRT_priv_task(ctx, ctx->req);
	} else {
		priv_task = VRT_priv_task(ctx, ctx->bo);
	}
	if (!priv_task->priv)
	{
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		try
		{
			auto prog = this->ref(ctx, debug);
			if (UNLIKELY(prog == nullptr))
				return nullptr;

			// Reserve a machine through blocking queue.
			// May throw if dequeue from the queue times out.
			Reservation resv = prog->reserve_vm(ctx, this, std::move(prog));
			// prog is nullptr after this ^

			priv_task->priv = resv.slot;
			priv_task->len  = KVM_PROGRAM_MAGIC;
#ifdef VARNISH_PLUS
			priv_task->free = resv.free;
#else
			auto* ptm = (struct vmod_priv_methods *)WS_Alloc(ctx->ws, sizeof(struct vmod_priv_methods));
			if (UNLIKELY(ptm == nullptr)) {
				resv.free(ctx, resv.slot);
				return nullptr;
			}
			ptm->magic = VMOD_PRIV_METHODS_MAGIC;
			ptm->type  = "vmod_kvm";
			ptm->fini  = resv.free;
			priv_task->methods = ptm;
#endif
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
	else
	{
		VSLb(ctx->vsl, SLT_Debug,
			"VM '%s' is being reused (already reserved)", config.name.c_str());
		//printf("KVM already reserved tenant %s\n", config.name.c_str());
	}
	return (VMPoolItem*) priv_task->priv;
}

VMPoolItem* TenantInstance::temporary_vmreserve(const vrt_ctx* ctx, bool debug, bool soft_reset)
{
	try
	{
		auto prog = this->ref(ctx, debug);
		if (UNLIKELY(prog == nullptr || ctx->ws == nullptr))
			return nullptr;

		// Reserve a machine through blocking queue.
		// May throw if dequeue from the queue times out.
		auto resv = prog->reserve_vm(ctx, this, std::move(prog), soft_reset);
		// prog is nullptr after this ^

		return resv.slot;

	} catch (std::exception& e) {
		// It makes no sense to reserve a VM without a request w/VSL
		VSLb(ctx->vsl, SLT_Error,
			"VM '%s' exception: %s", config.name.c_str(), e.what());
		return nullptr;
	}
}
void TenantInstance::temporary_vmreserve_free(const vrt_ctx* ctx, void* slotv)
{
	VMPoolItem *slot = (VMPoolItem *)slotv;

#ifdef VARNISH_PLUS
	(void)ctx;
	ProgramInstance::vm_free_function(slot);
#else
	ProgramInstance::vm_free_function(ctx, slot);
#endif
}

std::shared_ptr<ProgramInstance> TenantInstance::ref(const vrt_ctx *ctx, bool debug)
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

	return prog;
}

uint64_t TenantInstance::lookup(const char* name) const {
	auto inst = program;
	if (inst != nullptr)
		return inst->lookup(name);
	return 0x0;
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
std::string create_sha256_from_file(const std::string& filename)
{
	const int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		throw std::runtime_error("Could not open file for SHA256: " + filename);
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		close(fd);
		throw std::runtime_error("Could not stat file for SHA256: " + filename);
	}
	void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd); // Close immediately after mmap
	if (mapped == MAP_FAILED) {
		throw std::runtime_error("Could not mmap file for SHA256: " + filename);
	}

	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, mapped, st.st_size);
	SHA256_Final(hash, &sha256);
	munmap(mapped, st.st_size);

	static constexpr char lut[] = "0123456789abcdef";
	std::string hash_hex(SHA256_DIGEST_LENGTH * 2, 0);
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		hash_hex[i * 2]     = lut[(hash[i] >> 4) & 0x0F];
		hash_hex[i * 2 + 1] = lut[hash[i] & 0x0F];
	}
	return hash_hex;
}
std::string create_md5_from_file(const std::string& filename)
{
	const int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		throw std::runtime_error("Could not open file for MD5: " + filename);
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		close(fd);
		throw std::runtime_error("Could not stat file for MD5: " + filename);
	}
	void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd); // Close immediately after mmap
	if (mapped == MAP_FAILED) {
		throw std::runtime_error("Could not mmap file for MD5: " + filename);
	}

	unsigned char hash[MD5_DIGEST_LENGTH];
	MD5_CTX md5;
	MD5_Init(&md5);
	MD5_Update(&md5, mapped, st.st_size);
	MD5_Final(hash, &md5);
	munmap(mapped, st.st_size);

	static constexpr char lut[] = "0123456789abcdef";
	std::string hash_hex(MD5_DIGEST_LENGTH * 2, 0);
	for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		hash_hex[i * 2]     = lut[(hash[i] >> 4) & 0x0F];
		hash_hex[i * 2 + 1] = lut[hash[i] & 0x0F];
	}
	return hash_hex;
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
			inst->stats.live_update_transfer_bytes = res;
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
		/* Serialize and transfer state from old to new program */
		TenantInstance::serialize_storage_state(ctx, current, new_prog);
		/* Increment live-update counter from old to new program */
		new_prog->stats.live_updates = current->stats.live_updates + 1;
	}

	/* Swap out old program with new program. */
	if (!new_prog->main_vm->is_debug())
	{
		std::atomic_exchange(&this->program, new_prog);
	} else {
		std::atomic_exchange(&this->debug_program, new_prog);
	}
}

void TenantInstance::reload_program_live(VRT_CTX, bool debug)
{
	std::shared_ptr<ProgramInstance> null_prog = nullptr;
	std::shared_ptr<ProgramInstance> old_prog;

	/* This will unload the current program. */
	if (!debug) {
		old_prog = std::atomic_load(&this->program);
		std::atomic_exchange(&this->program, null_prog);
	} else {
		old_prog = std::atomic_load(&this->debug_program);
		std::atomic_exchange(&this->debug_program, null_prog);
	}

	/* XXX: There will be a few instances of denied requests.
	   This will cause the current program to be reinitialized
	   upon taking a reference. */
	this->m_started_init = false;

	/* No point in reloading the program if there's nothing to
	   serialize from the old storage to the new. It will be
	   loaded by the first request to it. */
	if (old_prog == nullptr || old_prog->has_storage() == false)
		return;

	/* Take a reference to new program (forcing it to reload). */
	if (auto new_prog = this->ref(ctx, debug))
	{
		/* Transfer storage state from old to new program. */
		TenantInstance::serialize_storage_state(
			ctx, old_prog, new_prog);
	}
}

} // kvm
