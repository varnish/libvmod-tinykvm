#include "tenants.hpp"

#include "common_defs.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <nlohmann/json.hpp>
#define KVM_TENANTS_MAGIC  0xc465573f

using json = nlohmann::json;
using namespace kvm;
namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);

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

static inline void kvm_load_tenant(
	VRT_CTX, VCL_PRIV task, kvm::TenantConfig&& config)
{
	try {
		tenancy(task).tenants.emplace(std::piecewise_construct,
			std::forward_as_tuple(config.name),
			std::forward_as_tuple(new TenantInstance(ctx, config))
		);
	} catch (const std::exception& e) {
		VRT_fail(ctx, "Exception when creating machine '%s': %s",
			config.name.c_str(), e.what());
	}
}

TenantInstance* kvm_create_temporary_tenant(VCL_PRIV task,
	const TenantInstance* vrm, const std::string& name)
{
	/* Create a new tenant with a temporary name,
	   and no program file to load. */
	TenantConfig config{vrm->config};
	config.name = name;
	config.filename = "";
	auto it = tenancy(task).temporaries.try_emplace(
		config.name,
		new TenantInstance(nullptr, config));
	return it.first->second;
}
void kvm_delete_temporary_tenant(VCL_PRIV task,
	const TenantInstance* ten)
{
	auto& temps = tenancy(task).temporaries;
	auto it = temps.find(ten->config.name);
	if (LIKELY(it != temps.end()))
	{
		assert(ten == it->second);
		delete ten;
		temps.erase(it);
		return;
	}
	throw std::runtime_error("Could not delete temporary tenant");
}

static void kvm_init_tenants(VRT_CTX, VCL_PRIV task,
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
				kvm_load_tenant(ctx, task, kvm::TenantConfig{
					it.key(),
					obj["filename"],
					obj["key"],
					group,
					tenancy(task).dynamic_functions
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
						group.max_boot_time =
							TenantGroup::to_ticks(obj["max_boot_time"]);
					}
				} else {
					VRT_fail(ctx, "Tenancy JSON %s: group '%s' has missing fields",
						source, it.key().c_str());
					return;
				}
			}
		}
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception '%s' when loading tenants from: %s",
			e.what(), source);
		/* TODO: VRT_fail here? */
		VRT_fail(ctx, "Exception '%s' when loading tenants from: %s",
			e.what(), source);
	}
}

} // kvm

extern "C"
kvm::TenantInstance* kvm_tenant_find(VCL_PRIV task, const char* name)
{
	auto& t = tenancy(task);
	if (UNLIKELY(name == nullptr))
		return nullptr;
	// regular tenants
	auto it = t.tenants.find(name);
	if (LIKELY(it != t.tenants.end()))
		return it->second;
	// temporary tenants (updates)
	it = t.temporaries.find(name);
	if (it != t.temporaries.end())
		return it->second;
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
	kvm_init_tenants(ctx, task, json, "string");
}

extern "C"
void kvm_init_tenants_file(VRT_CTX, VCL_PRIV task, const char* filename)
{
	const auto json = kvm::file_loader(filename);
	kvm_init_tenants(ctx, task, json, filename);
}
