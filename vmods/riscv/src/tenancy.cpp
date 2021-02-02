#include "sandbox.hpp"
#include "varnish.hpp"
#include <EASTL/string_map.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
extern std::vector<uint8_t> file_loader(const std::string& filename);

using MapType = eastl::string_map<struct vmod_riscv_machine*>;
static MapType temporaries;

inline MapType& tenants(VRT_CTX)
{
	(void) ctx;
	static MapType t;
	return t;
}

inline void load_tenant(VRT_CTX, TenantConfig&& config)
{
	try {
		tenants(ctx).try_emplace(
			strdup(config.name.c_str()),
			new vmod_riscv_machine(ctx, config));
	} catch (const std::exception& e) {
		VRT_fail(ctx, "Exception when creating machine '%s': %s",
			config.name.c_str(), e.what());
	}
}

extern "C"
vmod_riscv_machine* tenant_find(VRT_CTX, const char* name)
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

vmod_riscv_machine* create_temporary_tenant(
	const vmod_riscv_machine* vrm, const std::string& name)
{
	/* Create a new tenant with a temporary name,
	   and no program file to load. */
	TenantConfig config{vrm->config};
	config.name = name;
	config.filename = "";
	auto it = temporaries.try_emplace(
		strdup(config.name.c_str()),
		new vmod_riscv_machine(nullptr, config));
	return it.first->second;
}
void delete_temporary_tenant(const vmod_riscv_machine* vrm)
{
	auto it = temporaries.find(vrm->config.name.c_str());
	if (it != temporaries.end())
	{
		assert(vrm == it->second);
		delete vrm;
		temporaries.erase(it);
		return;
	}
	throw std::runtime_error("Could not delete temporary tenant");
}

static void init_tenants(VRT_CTX,
	const std::vector<uint8_t>& vec, const char* source)
{
	try {
		const json j = json::parse(vec.begin(), vec.end());

		std::map<std::string, TenantConfig> groups;

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
				load_tenant(ctx, TenantConfig{
					.name     = it.key(),
					.group    = obj["group"],
					.filename = obj["filename"],
					.max_instructions = group.max_instructions,
					.max_memory = group.max_memory,
					.max_heap   = group.max_heap
				});
			} else {
				if (obj.contains("max_instructions") &&
					obj.contains("max_memory") &&
					obj.contains("max_heap"))
				{
					groups[it.key()] = TenantConfig{
						.max_instructions = obj["max_instructions"],
						.max_memory = obj["max_memory"],
						.max_heap   = obj["max_heap"]
					};
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
void init_tenants_str(VRT_CTX, const char* str)
{
	std::vector<uint8_t> json { str, str + strlen(str) };
	init_tenants(ctx, json, "string");
}

extern "C"
void init_tenants_file(VRT_CTX, const char* filename)
{
	const auto json = file_loader(filename);
	init_tenants(ctx, json, filename);
}
