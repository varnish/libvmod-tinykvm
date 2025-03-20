/**
 * @file tenant.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Tenant JSON parsing and startup configuration.
 * @version 0.1
 * @date 2022-10-13
 *
 * The functions kvm_init_tenants_str and kvm_init_tenants_file will
 * be called during startup, from VCL. The functions will parse a JSON
 * document configuring each tenant, and then create tenants one by one.
 *
 * The JSON document can have tenants and groups. Each group is a named
 * object with settings, but no filename/key. Each named object with a
 * filename and key is treated as a tenant. You can customize each tenant
 * by adding settings directly to the tenant, just like a group.
 *
 * Example:
 * 	kvm.embed_tenants("""{
 *		"test.com": {
 *			"key": "123",
 *			"group": "test",
 *			"filename": "/tmp/test",
 *
 *			"concurrency": 4,
 *			"max_boot_time": 16.0,
 *			"max_memory": 512,
 *			"max_request_time": 4.0,
 *			"max_request_memory": 128,
 *		}
 *	}""");
 *
 * The JSON above will create a test.com tenant with 32 request VMs that
 * each have up to 128MB memory. It will also have a main storage VM with
 * up to 512MB memory. Request VMs and storage VM share the same memory,
 * however request VMs cannot modify main memory, only make local copies
 * of pages. That is where max_work_memory comes in: It is the amount of
 * memory that each request VM can create from duplicating main memory
 * when writes occur.
 * 
 * The timeouts are used to determine the length of time we are allowed
 * to use to initialize the main storage VM as well as handle requests.
 * max_boot_time is used during initialization.
 * max_request_time is used during GET/POST request handling.
 * 
 * Each tenants main VM is initialized at the same time as other tenants,
 * in parallel. This reduces startup time greatly when there are many
 * tenants. Should one of the tenants fail in a way that causes a wait
 * for the full duration of the max_boot_time, then Varnish will appear
 * to hang for that duration, as it will have to wait until everyone is
 * either ready, or has failed. It is therefore probably in everyones
 * best interest to not make the boot initialization timeout too high.
 * 
 */
#include "tenants.hpp"

#include "common_defs.hpp"
#include "curl_fetch.hpp"
#include "tenant_instance.hpp"
#include "utils/crc32.hpp"
#include "varnish.hpp"
#include <string_view>
#include <thread>
#include <nlohmann/json.hpp>
#define KVM_TENANTS_MAGIC  0xc465573f
using json = nlohmann::json;

namespace kvm {
extern std::vector<uint8_t> file_loader(const std::string&);
const std::string TenantConfig::guest_state_file = "state";

TenantConfig::TenantConfig(
	std::string n, std::string f, std::string k,
	TenantGroup grp, std::string uri)
	: name(std::move(n)), hash{crc32c_hw(n)}, group{std::move(grp)},
	  filename(std::move(f)), key(std::move(k)),
	  uri(std::move(uri))
{
	this->allowed_file = filename + ".state";
}
TenantConfig::~TenantConfig() {}

/* Find the active VCL tenants structure. Lazy instantiation. */
Tenants& tenancy(VCL_PRIV task)
{
	if (LIKELY(task->priv != nullptr)) {
		return *(Tenants *)task->priv;
	}
	task->priv = new Tenants{};
	task->len  = KVM_TENANTS_MAGIC;
#ifdef VARNISH_PLUS
	task->free = [] (void* priv) {
		auto* tenants = (Tenants*)priv;
		std::thread thr{[tenants] {
			delete tenants;
		}};
		thr.detach();
	};
#else
	auto* ptm = new vmod_priv_methods {};
	ptm->magic = VMOD_PRIV_METHODS_MAGIC;
	ptm->type  = "vmod_kvm_tenants";
	ptm->fini  = [] (auto*, void* priv) {
		auto* tenants = (Tenants*)priv;
		std::thread thr{[tenants] {
			delete tenants;
		}};
		thr.detach();
	};
	task->methods = ptm;
#endif
	return *(Tenants *)task->priv;
}

static inline bool load_tenant(VRT_CTX, VCL_PRIV task,
	const kvm::TenantConfig& config, bool initialize)
{
	try {
		auto& tcy = tenancy(task);
		/* Create hash from tenant/program name. */
		const uint32_t hash = crc32c_hw(config.name);

		/* Insert tenant/program as VCL tenant instance. */
		const auto [it, inserted] =
			tcy.tenants.try_emplace(hash, std::move(config));
		if (UNLIKELY(!inserted)) {
			throw std::runtime_error("Tenant already existed: " + config.name);
		}
		/* If initialization needed, create program immediately. */
		if (initialize) {
			const bool debug = false;
			it->second.begin_initialize(ctx, debug);
		}
		return true;

	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"kvm: Exception when creating tenant '%s': %s",
			config.name.c_str(), e.what());
		VRT_fail(ctx, "kvm: Exception when creating tenant '%s': %s",
			config.name.c_str(), e.what());
		return false;
	}
}

template <typename It>
static void configure_group(const std::string& name, kvm::TenantGroup& group, const It& obj)
{
	// All group parameters are treated as optional and can be defined in a
	// tenant configuration or in a group configuration.
	if (obj.key() == "max_boot_time")
	{
		group.max_boot_time = obj.value();
	}
	else if (obj.key() == "max_request_time")
	{
		group.max_req_time = obj.value();
	}
	else if (obj.key() == "max_storage_time")
	{
		group.max_req_time = obj.value();
	}
	else if (obj.key() == "max_memory")
	{
		// Limits the memory of the Main VM.
		group.set_max_memory(obj.value());
	}
	else if (obj.key() == "max_request_memory")
	{
		// Limits the memory of an ephemeral VM. Ephemeral VMs are used to handle
		// requests (and faults in pages one by one using CoW). They are based
		// off of the bigger Main VMs which use "max_memory" (and are identity-mapped).
		group.set_max_workmem(obj.value());
	}
	else if (obj.key() == "req_mem_limit_after_reset")
	{
		// Limits the memory of an ephemeral VM after request completion.
		// Without a limit, the request memory is kept in order to make future
		// requests faster due to not having to create memory banks.
		group.set_limit_workmem_after_req(obj.value());
	}
	else if (obj.key() == "shared_memory")
	{
		// Sets the size of shared memory between VMs.
		// Cannot be larger than half of max memory.
		group.set_shared_mem(obj.value());
	}
	else if (obj.key() == "concurrency")
	{
		group.max_concurrency = obj.value();
	}
	else if (obj.key() == "storage")
	{
		group.has_storage = obj.value();
	}
	else if (obj.key() == "hugepages")
	{
		group.hugepages = obj.value();
	}
	else if (obj.key() == "request_hugepages")
	{
		group.ephemeral_hugepages = obj.value();
	}
	else if (obj.key() == "stdout")
	{
		group.print_stdout = obj.value();
	}
	else if (obj.key() == "smp")
	{
		group.max_smp = obj.value();
		// TinyKVM does not support more than 16 extra vCPUs (for now)
		group.max_smp = std::min(size_t(16), group.max_smp);
	}
	else if (obj.key() == "allow_debug")
	{
		group.allow_debug = obj.value();
	}
	else if (obj.key() == "control_ephemeral")
	{
		// Allow guest to control ephemeral using system call
		group.control_ephemeral = obj.value();
	}
	else if (obj.key() == "ephemeral")
	{
		// Set the default ephemeralness for this group/tenant
		group.ephemeral = obj.value();
	}
	else if (obj.key() == "environment")
	{
		// Append environment variables (NOTE: unable to overwrite defaults)
		auto vec = obj.value().template get<std::vector<std::string>>();
		group.environ.insert(group.environ.end(), vec.begin(), vec.end());
	}
	else if (obj.key() == "remapping" || obj.key() == "executable_remapping")
	{
		// Append remappings
		auto entry = obj.value().template get<std::pair<std::string, uint32_t>>();
		char *end;
		unsigned long long address = strtoull(entry.first.c_str(), &end, 16);
		if (address < 0x20000) {
			throw std::runtime_error("Remapping address was not a number, or invalid");
		} else if (errno != 0) {
			throw std::runtime_error("Remapping does not fit in 64-bit address");
		}
		tinykvm::VirtualRemapping vmem {
			.phys = 0x0,
			.virt = address,
			.size = entry.second << 20U,
			.writable   = true,
			.executable = obj.key() == "executable_remapping"
		};
		group.vmem_remappings.push_back(vmem);
		//printf("Remapping at 0x%llX  size 0x%X\n",
		//	group.vmem_remappings.back().virt,
		//	group.vmem_remappings.back().size);
	}
	else if (obj.key() == "executable_heap")
	{
		group.vmem_heap_executable = obj.value();
	}
	else if (obj.key() == "allowed_paths")
	{
		group.allowed_paths = obj.value().template get<std::vector<std::string>>();
	}
	else if (obj.key() == "verbose") {
		group.verbose = obj.value();
	}
	else if (obj.key() == "group") { /* Silently ignore. */ }
	else if (obj.key() == "key")   { /* Silently ignore. */ }
	else if (obj.key() == "uri")   { /* Silently ignore. */ }
	else if (obj.key() == "filename") { /* Silently ignore. */ }
	else
	{
		VSL(SLT_Error, 0,
			"kvm: Unknown configuration key for '%s': %s",
			name.c_str(), obj.key().c_str());
		fprintf(stderr,
			"kvm: Unknown configuration key for '%s': %s\n",
			name.c_str(), obj.key().c_str());
	}
}

/* This function is not strictly necessary - we are just trying to find the intention of
   the user. If any of these are present, we believe the intention of the user is to
   create a program definition. However, if group is missing, it is ultimately incomplete. */
template <typename T>
static inline bool is_tenant(const T& obj)
{
	return obj.contains("group") || obj.contains("filename") || obj.contains("uri");
}

static void init_tenants(VRT_CTX, VCL_PRIV task,
	const std::string_view json_strview, const char* source, bool initialize)
{
	(void) source;
	/* Parse JSON with comments enabled. */
	const json j = json::parse(json_strview.begin(), json_strview.end(), nullptr, true, true);

	// The test group is automatically created using defaults
	std::map<std::string, kvm::TenantGroup> groups { {"test", kvm::TenantGroup{"test"}} };

	// Iterate through the user-defined tenant configuration.
	// The configuration can either be a tenant of a group,
	// or the configuration for group.
	//
	// The type of configuration is determined by the presence of specific
	// keys. If "group" is present, then we have the
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
		if (is_tenant(obj)) continue;

		const auto& grname = it.key();
		auto grit = groups.find(grname);

		if (grit == groups.end()) {
			const auto& ret = groups.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(grname),
				std::forward_as_tuple(grname));
			grit = ret.first;
		}
		auto& group = grit->second;

		// Set group settings
		for (auto it = obj.begin(); it != obj.end(); ++it) {
			configure_group(grname, group, it);
		}
	}
	for (const auto& it : j.items())
	{
		const auto& obj = it.value();
		// Tenant configuration
		if (is_tenant(obj))
		{
			const std::string grname =
				!obj.contains("group") ? "test" : obj["group"];
			auto grit = groups.find(grname);
			if (UNLIKELY(grit == groups.end())) {
				throw std::runtime_error("Could not find group " + grname + " for '" + it.key() + "'");
			}
			// Make a copy of the selected group
			auto group = grit->second;

			// Override both group and tenant settings in one place
			for (auto it = obj.begin(); it != obj.end(); ++it) {
				configure_group(grname, group, it);
			}

			/* Filenames are optional. */
			std::string filename = "";
			if (obj.contains("filename")) filename = obj["filename"];
			/* Keys are optional. No/empty key = no live update. */
			std::string lvu_key = "";
			if (obj.contains("key")) lvu_key = obj["key"];
			/* URI is used to fetch a program remotely. */
			std::string uri = "";
			if (obj.contains("uri")) uri = obj["uri"];
			/* Verify: No filename and no key is an unreachable program. */
			if (filename.empty() && uri.empty())
				throw std::runtime_error("kvm: Unreachable program " + it.key() + " has no URI or filename");

			/* Use the group data except filename */
			kvm::load_tenant(ctx, task, kvm::TenantConfig{
				it.key(),
				std::move(filename),
				std::move(lvu_key),
				std::move(group),
				std::move(uri)
			}, initialize);
		}
	}

	/* Skip initialization here if not @initialize.
	   NOTE: Early return.  */
	if (initialize == false)
		return;

	/* Finish initialization, but do not VRT_fail if program
		initialization failed. It is a recoverable error. */
	for (auto& it : tenancy(task).tenants) {
		auto& tenant = it.second;
		try {
			tenant.wait_for_initialization();
		}
		catch (const std::exception &e) {
			VSL(SLT_Error, 0,
				"Exception when creating machine '%s' from source '%s': %s",
				tenant.config.name.c_str(), tenant.config.filename.c_str(), e.what());
			fprintf(stderr,
				"Exception when creating machine '%s' from source '%s': %s\n",
				tenant.config.name.c_str(), tenant.config.filename.c_str(), e.what());
			/* XXX: This can be racy if the same tenant is specified
				more than once, and is still initializing... */
			tenant.program = nullptr;
		}
	}
}

struct FetchTenantsStuff {
	VRT_CTX;
	VCL_PRIV task;
	VCL_STRING url;
	bool init;
};

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
	return nullptr;
}

using foreach_function_t = int(*)(const char *, kvm::TenantInstance*, void *);
extern "C"
int kvm_tenant_foreach(VCL_PRIV task, foreach_function_t func, void* state)
{
	auto& t = kvm::tenancy(task);
	if (UNLIKELY(func == nullptr))
		return 0;
	int count = 0;
	for (auto& it : t.tenants) {
		count += func(it.second.config.name.c_str(), &it.second, state);
	}
	return count;
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
int kvm_init_tenants_str(VRT_CTX, VCL_PRIV task, const char* filename,
	const char* str, size_t len, int init)
{
	/* Load tenants from a JSON string, with the filename used for logging purposes. */
	try {
		const std::string_view json { str, len };
		kvm::init_tenants(ctx, task, json, filename, init);
		return 1;
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"kvm: Exception when loading tenants from string '%s': %s",
			filename, e.what());
		fprintf(stderr,
			"kvm: Exception when loading tenants from string '%s': %s\n",
			filename, e.what());
		return 0;
	}
}

extern "C"
int kvm_init_tenants_file(VRT_CTX, VCL_PRIV task, const char* filename, int init)
{
	/* Load tenants from a local JSON file. */
	try {
		const auto json = kvm::file_loader(filename);
		const std::string_view json_strview {(const char *)json.data(), json.size()};
		kvm::init_tenants(ctx, task, json_strview, filename, init);
		return 1;
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"kvm: Exception when loading tenants from file '%s': %s",
			filename, e.what());
		fprintf(stderr,
			"kvm: Exception when loading tenants from file '%s': %s\n",
			filename, e.what());
		return 0;
	}
}

extern "C"
int kvm_init_tenants_uri(VRT_CTX, VCL_PRIV task, const char* uri, int init)
{
	/* Load tenants from a remote JSON file. */
	kvm::FetchTenantsStuff ftd = {
		.ctx = ctx,
		.task = task,
		.url = uri,
		.init = (bool)init,
	};
	long res = kvm_curl_fetch(uri,
	[] (void* usr, long, struct MemoryStruct *chunk) {
		auto* ftd = (kvm::FetchTenantsStuff *)usr;
		const std::string_view json { chunk->memory, chunk->size };
		try {
			kvm::init_tenants(ftd->ctx, ftd->task, json, ftd->url, ftd->init);
		} catch (const std::exception& e) {
			VSL(SLT_Error, 0,
				"kvm: Exception when loading tenants from URI '%s': %s",
				ftd->url, e.what());
			fprintf(stderr,
				"kvm: Exception when loading tenants from URI '%s': %s\n",
				ftd->url, e.what());
		}
	}, &ftd);
	return (res == 0);
}

extern "C"
int kvm_tenant_configure(VRT_CTX, kvm::TenantInstance* ten, const char* str)
{
	(void)ctx;
	/* Override program configuration from a JSON string. */
	try {
		const std::string_view jstr { str, __builtin_strlen(str) };
		/* Parse JSON with comments enabled. */
		const json j = json::parse(jstr.begin(), jstr.end(), nullptr, true, true);
		/* Iterate through all elements, pass to tenants "group" config. */
		for (auto it = j.begin(); it != j.end(); ++it) {
			kvm::configure_group(ten->config.name, ten->config.group, it);
		}
		return 1;
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"kvm: Exception when overriding program configuration '%s': %s",
			ten->config.name.c_str(), e.what());
		VSL(SLT_Error, 0, "JSON: %s\n", str);
		fprintf(stderr,
			"kvm: Exception when overriding program configuration '%s': %s\n",
			ten->config.name.c_str(), e.what());
		fprintf(stderr, "JSON: %s\n", str);
		return 0;
	}
}

extern "C"
int kvm_tenant_arguments(VRT_CTX, kvm::TenantInstance* ten, size_t n, const char** strings)
{
	(void)ctx;
	/* Set program main() argument from a string. */
	try {
		auto vec = std::make_shared<std::vector<std::string>> ();
		vec->reserve(n);
		for (size_t i = 0; i < n; i++) {
			const char* str = strings[i] != nullptr ? strings[i] : "";
			vec->push_back(str);
		}
		std::atomic_exchange(&ten->config.group.main_arguments, vec);
		return 1;
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"kvm: Exception when adding program argument '%s': %s",
			ten->config.name.c_str(), e.what());
		fprintf(stderr,
			"kvm: Exception when adding program argument '%s': %s\n",
			ten->config.name.c_str(), e.what());
		return 0;
	}
}
