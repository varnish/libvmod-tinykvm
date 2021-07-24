#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace kvm;
namespace kvm {
	extern std::vector<uint8_t> file_loader(const std::string&);
}

using MapType = std::unordered_map<std::string, kvm::TenantInstance*>;
static MapType temporaries;

static inline MapType& tenants(VRT_CTX)
{
	(void) ctx;
	static MapType t;
	return t;
}

static inline void kvm_load_tenant(VRT_CTX, kvm::TenantConfig&& config)
{
	try {
		tenants(ctx).try_emplace(
			config.name,
			new TenantInstance(ctx, config));
	} catch (const std::exception& e) {
		VRT_fail(ctx, "Exception when creating machine '%s': %s",
			config.name.c_str(), e.what());
	}
}

extern "C"
kvm::TenantInstance* kvm_tenant_find(VRT_CTX, const char* name)
{
	auto& map = tenants(ctx);
	// regular tenants
	auto it = map.find(name);
	if (it != map.end())
		return it->second;
	// temporary tenants (updates)
	it = temporaries.find(name);
	if (it != map.end())
		return it->second;
	return nullptr;
}

TenantInstance* kvm_create_temporary_tenant(
	const TenantInstance* vrm, const std::string& name)
{
	/* Create a new tenant with a temporary name,
	   and no program file to load. */
	TenantConfig config{vrm->config};
	config.name = name;
	config.filename = "";
	auto it = temporaries.try_emplace(
		config.name,
		new TenantInstance(nullptr, config));
	return it.first->second;
}
void kvm_delete_temporary_tenant(const TenantInstance* vrm)
{
	auto it = temporaries.find(vrm->config.name);
	if (it != temporaries.end())
	{
		assert(vrm == it->second);
		delete vrm;
		temporaries.erase(it);
		return;
	}
	throw std::runtime_error("Could not delete temporary tenant");
}

static void kvm_init_tenants(VRT_CTX,
	const std::vector<uint8_t>& vec, const char* source)
{
	try {
		const json j = json::parse(vec.begin(), vec.end());

		std::map<std::string, kvm::TenantGroup> groups {
			{"test", kvm::TenantGroup{
				"test",
				256, /* Milliseconds */
				256 * 1024 * 1024
			}}
		};

		for (const auto& it : j.items())
		{
			const auto& obj = it.value();
			if (obj.contains("filename") && obj.contains("group"))
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
				kvm_load_tenant(ctx, kvm::TenantConfig{
					it.key(), obj["filename"], group,
				});
			} else {
				if (obj.contains("max_time") &&
					obj.contains("max_memory"))
				{
					groups.emplace(std::piecewise_construct,
						std::forward_as_tuple(it.key()),
						std::forward_as_tuple(
							it.key(),
							obj["max_time"],
							obj["max_memory"]
						));
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

extern "C"
void kvm_init_tenants_str(VRT_CTX, const char* str)
{
	const std::vector<uint8_t> json { str, str + strlen(str) };
	kvm_init_tenants(ctx, json, "string");
}

extern "C"
void kvm_init_tenants_file(VRT_CTX, const char* filename)
{
	const auto json = kvm::file_loader(filename);
	kvm_init_tenants(ctx, json, filename);
}
