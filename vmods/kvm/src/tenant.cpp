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

static const TenantGroup test_group {
	"test",
	1.0f, /* Seconds */
	256 * 1024, /* 256MB main memory */
	4 * 1024, /* 4MB working memory */
};

Tenants& tenancy(VCL_PRIV task)
{
	if (LIKELY(task->priv != nullptr)) {
		return *(Tenants *)task->priv;
	}
	task->priv = new Tenants{};
	task->len  = KVM_TENANTS_MAGIC;
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
	VRT_CTX, VCL_PRIV task, kvm::TenantConfig&& config)
{
	try {
		auto& tcy = tenancy(task);
		const uint32_t hash = crc32c_hw(config.name);

		const auto [it, inserted] =
			tcy.tenants.try_emplace(hash, ctx, config);
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

		std::map<std::string, kvm::TenantGroup> groups {
			{"test", test_group}
		};

		for (const auto& it : j.items())
		{
			const auto& obj = it.value();
			if (obj.contains("filename")
				&& obj.contains("key")
				&& obj.contains("group"))
			{
				const std::string& grname = obj["group"];
				auto grit = groups.find(grname);
				/* Validate the group name */
				if (grit == groups.end()) {
					VSL(SLT_Error, 0,
						"Group '%s' missing for tenant: %s",
						grname.c_str(), it.key().c_str());
					continue;
				}
				const auto& group = grit->second;
				/* Use the group data except filename */
				kvm::load_tenant(ctx, task, kvm::TenantConfig{
					it.key(),
					obj["filename"],
					obj["key"],
					group,
					kvm::tenancy(task).dynamic_functions
				});
			} else {
				if (obj.contains("max_time") &&
					obj.contains("max_memory") &&
					obj.contains("max_work_memory"))
				{
					auto ins =
					groups.emplace(std::piecewise_construct,
						std::forward_as_tuple(it.key()),
						std::forward_as_tuple(
							it.key(),
							obj["max_time"],
							obj["max_memory"],
							obj["max_work_memory"]
						));
					auto& group = ins.first->second;
					/* Optional group settings */
					if (obj.contains("max_boot_time")) {
						group.max_boot_time = obj["max_boot_time"];
					}
					if (obj.contains("max_concurrency")) {
						group.max_concurrency = obj["max_concurrency"];
					}
					if (obj.contains("hugepages")) {
						group.hugepages = obj["hugepages"];
					}
				} else {
					VSL(SLT_Error, 0,
						"Tenancy JSON %s: group '%s' has missing fields",
						source, it.key().c_str());
					continue;
				}
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
