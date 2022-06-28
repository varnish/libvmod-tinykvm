#include "tenants.hpp"

#include "common_defs.hpp"
#include "tenant_instance.hpp"
#include "utils/crc32.hpp"
#include "varnish.hpp"
#include <nlohmann/json.hpp>
#define KVM_TENANTS_MAGIC  0xc465573f
using json = nlohmann::json;

namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);
const std::string TenantConfig::guest_state_file = "state";

TenantConfig::TenantConfig(
	std::string n, std::string f, std::string k,
	TenantGroup g, dynfun_map& dfm)
	: name(n), filename(f), key(k), hash{crc32c_hw(n)}, group{std::move(g)}, dynamic_functions_ref{dfm}
{
	this->allowed_file = filename + ".state";
}

TenantConfig::~TenantConfig() {}

Tenants& tenancy(VCL_PRIV task)
{
	if (LIKELY(task->priv != nullptr)) {
		return *(Tenants *)task->priv;
	}
	task->priv = new Tenants{};
	task->len  = KVM_TENANTS_MAGIC;
	task->free = [] (void* priv) {
		delete (Tenants*)priv;
	};
	return *(Tenants *)task->priv;
}

bool TenantConfig::begin_dyncall_initialization(VCL_PRIV task)
{
	auto& tcy = tenancy(task);
	if (tcy.dyncalls_initialized) return false;
	tcy.dyncalls_initialized = true;
	return true;
}

static inline bool load_tenant(
	VRT_CTX, VCL_PRIV task, const kvm::TenantConfig& config)
{
	try {
		auto& tcy = tenancy(task);
		const uint32_t hash = crc32c_hw(config.name);

		const auto [it, inserted] =
			tcy.tenants.try_emplace(hash, ctx, std::move(config));
		if (UNLIKELY(!inserted)) {
			throw std::runtime_error("Tenant already existed: " + config.name);
		}
		return true;

	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating tenant '%s': %s",
			config.name.c_str(), e.what());
		VRT_fail(ctx, "Exception when creating tenant '%s': %s",
			config.name.c_str(), e.what());
		return false;
	}
}

TenantInstance* create_temporary_tenant(VCL_PRIV task,
	const TenantInstance* vrm, const std::string& name)
{
	/* Create a new tenant with a temporary name,
		 and no program file to load. */
	TenantConfig config{vrm->config};
	config.name = name;
	config.filename = "";
	config.hash = crc32c_hw(name);
	auto it = tenancy(task).temporaries.try_emplace(
		config.hash,
		nullptr, config);
	return &it.first->second;
}
void delete_temporary_tenant(VCL_PRIV task,
	const TenantInstance* ten)
{
	auto& temps = tenancy(task).temporaries;
	auto it = temps.find(ten->config.hash);
	if (LIKELY(it != temps.end()))
	{
		assert(ten->config.name == it->second.config.name);
		delete ten;
		temps.erase(it);
		return;
	}
	throw std::runtime_error(
		"Could not delete temporary tenant: " + ten->config.name);
}

static void init_tenants(VRT_CTX, VCL_PRIV task,
	const std::vector<uint8_t>& vec, const char* source)
{
	try {
		const json j = json::parse(vec.begin(), vec.end());

		std::map<std::string, kvm::TenantGroup> groups {};

		// Iterate through the user-defined tenant configuration.
		// The configuration can either be a tenant of a group,
		// or the configuration for group.
		//
		// The type of configuration is determined by the presence of specific
		// keys. If "filename", "key", and "group" are present, then we have the
		// configuration for a tenant in a group. Otherwise, we assume it is
		// the configuration for a group.
		//
		// The configuration for a group will affect all the tenants that are
		// associated with that group.
		//
		// Tenants are identified through CRC32-C checksums, which means that there
		// is a miniscule possibility of collisions when adding a million tenants.
		// But, in practice it should not happen. And the algorithm is entirely
		// internal and can be changed without breaking anyones setup. Importantly,
		// CRC32-C is hardware accelerated and faster than string matching.
		for (const auto& it : j.items())
		{
			const auto& obj = it.value();
			std::string grname;
			// We either have a group configuration or tenant configuration.
			// Check for the existence of the group and create it if it does not exist.
			// The ordering of the tenants can be such that the group configuration
			// comes after the tenants, or there is no group configuration.
			//
			// If the object key is a valid group, we assume this is configuration
			// that is specific to the group.
			//
			// Otherwise, it must be the tenant configuration.
			if (obj.contains("group")) {
				grname = obj["group"];
			} else {
				grname = it.key();
			}
			auto grit = groups.find(grname);
			if (grit == groups.end()) {
				const auto& ret = groups.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(grname),
					std::forward_as_tuple(
						grname,
						1.0f, /* 1 second timeout */
						256, /* 256 MB max memory */
						4 /* 4 MB max working memory */
				));
				grit = ret.first;
			}
			auto& group = grit->second;
			// All group parameters are treated as optional and can be defined in a
			// tenant configuration or in a group configuration.
			if (obj.contains("max_time")) {
				group.max_time = obj["max_time"];
			}
			if (obj.contains("max_memory")) {
				// Limits the memory of the Main VM.
				group.set_max_memory(obj["max_memory"]);
			}
			if (obj.contains("shared_memory")) {
				// Sets the size of shared memory between VMs.
				// Cannot be larger than max memory.
				group.set_max_memory(obj["shared_memory"]);
			}
			if (obj.contains("max_work_memory")) {
				// Limits the memory of an ephemeral VM. Ephemeral VMs are used to handle
				// requests (and faults in pages one by one using CoW). They are based
				// off of the bigger Main VMs which use "max_memory" (and are identity-mapped).
				group.set_max_workmem(obj["max_work_memory"]);
			}
			if (obj.contains("max_boot_time")) {
				group.max_boot_time = obj["max_boot_time"];
			}
			if (obj.contains("max_concurrency")) {
				group.max_concurrency = obj["max_concurrency"];
			}
			if (obj.contains("hugepages")) {
				group.hugepages = obj["hugepages"];
			}
			if (obj.contains("allowed_paths")) {
				group.allowed_paths = obj["allowed_paths"].get<std::vector<std::string>>();
			}
			// Tenant configuration
			if (obj.contains("key") && obj.contains("group"))
			{
				/* Filenames are optional. */
				std::string filename = "";
				if (obj.contains("filename")) filename = obj["filename"];
				/* Use the group data except filename */
				kvm::load_tenant(ctx, task, kvm::TenantConfig{
					it.key(),
					filename,
					obj["key"],
					group,
					kvm::tenancy(task).dynamic_functions
				});
			}
		}
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"vmod_kvm: Exception when loading tenants from %s: %s",
			source, e.what());
		VRT_fail(ctx,
			"vmod_kvm: Exception when loading tenants from %s: %s",
			source, e.what());
	}
}

} // kvm

extern "C"
kvm::TenantInstance* kvm_tenant_find(VCL_PRIV task, const char* name)
{
	auto& t = kvm::tenancy(task);
	if (UNLIKELY(name == nullptr))
		return nullptr;
	const uint32_t hash = kvm::crc32c_hw(name, strlen(name));
	// regular tenants
	auto it = t.tenants.find(hash);
	if (LIKELY(it != t.tenants.end()))
		return &it->second;
	// temporary tenants (updates)
	it = t.temporaries.find(hash);
	if (it != t.temporaries.end())
		return &it->second;
	return nullptr;
}

extern "C"
kvm::TenantInstance* kvm_tenant_find_key(VCL_PRIV task, const char* name, const char* key)
{
	auto* tenant = kvm_tenant_find(task, name);
	if (tenant != nullptr) {
		if (tenant->config.key == key) return tenant;
	}
	return nullptr;
}

extern "C"
void kvm_init_tenants_str(VRT_CTX, VCL_PRIV task, const char* str)
{
	const std::vector<uint8_t> json { str, str + strlen(str) };
	kvm::init_tenants(ctx, task, json, "string");
}

extern "C"
void kvm_init_tenants_file(VRT_CTX, VCL_PRIV task, const char* filename)
{
	try {
		const auto json = kvm::file_loader(filename);
		kvm::init_tenants(ctx, task, json, filename);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"vmod_kvm: Exception when loading tenants from file '%s': %s",
			filename, e.what());
		VRT_fail(ctx,
			"vmod_kvm: Exception when loading tenants from file '%s': %s",
			filename, e.what());
	}
}
