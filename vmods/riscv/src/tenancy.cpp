#include "sandbox.hpp"
#include <EASTL/string_map.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
extern std::vector<uint8_t> file_loader(const std::string& filename);

using MapType = eastl::string_map<struct vmod_riscv_machine*>;
inline MapType& tenants(VRT_CTX)
{
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
	auto it = map.find(name);
	if (it != map.end())
		return it->second;
	return nullptr;
}

extern "C"
void init_tenants(VRT_CTX, const char* filename)
{
	try {
		const auto vec = file_loader(filename);
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
						filename, it.key().c_str());
					return;
				}
			}
		}
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception '%s' when loading tenants from: %s",
			e.what(), filename);
		/* TODO: VRT_fail here? */
		VRT_fail(ctx, "Exception '%s' when loading tenants from: %s",
			e.what(), filename);
	}
}
