#include "tenant_instance.hpp"
#include "varnish.hpp"
#include "utils/crc32.hpp"
static constexpr bool FAST_RESET_METHOD = true;

namespace kvm {
	extern std::vector<uint8_t> file_loader(const std::string&);
	extern void initialize_vmods(VRT_CTX);
	std::unordered_map<uint32_t, TenantInstance::ghandler_t> TenantInstance::m_dynamic_functions;

TenantInstance::TenantInstance(VRT_CTX, const TenantConfig& conf)
	: config{conf}
{
	static bool init = false;
	if (!init) {
		init = true;
		MachineInstance::kvm_initialize();
		initialize_vmods(ctx);
	}

	try {
		auto elf = file_loader(conf.filename);
		this->program =
			std::make_shared<ProgramInstance> (std::move(elf), ctx, this);
	} catch (const std::exception& e) {
		VSL(SLT_Error, 0,
			"Exception when creating machine '%s': %s",
			conf.name.c_str(), e.what());
		this->program = nullptr;
	}
}

MachineInstance* TenantInstance::vmfork(const vrt_ctx* ctx, bool debug)
{
	auto* priv_task = VRT_priv_task(ctx, ctx);
	if (!priv_task->priv)
	{
	#ifdef ENABLE_TIMING
		TIMING_LOCATION(t0);
	#endif
		std::shared_ptr<ProgramInstance> prog;
		if (!debug)
			prog = this->program;
		else
			prog = this->debug_program;
		/* First-time tenants could have no program */
		if (UNLIKELY(prog == nullptr)) {
			VRT_fail(ctx, "vmfork: Missing program for %s. Not uploaded?",
				config.name.c_str());
			return nullptr;
		}
		try {
			MachineInstance* inst;
			if constexpr (FAST_RESET_METHOD) {
				/* Get free instance through concurrent queue */
				inst = prog->concurrent_fork(ctx, this, prog);
			} else {
				/* Create new instance on workspace by forking */
				inst = prog->workspace_fork(ctx, this, prog);
			}

			priv_task->priv = inst;
			priv_task->len  = KVM_PROGRAM_MAGIC;
			priv_task->free = [] (void* inst) {
			#ifdef ENABLE_TIMING
				TIMING_LOCATION(t2);
			#endif
				auto* mi = (MachineInstance *)inst;
				if constexpr (FAST_RESET_METHOD) {
					mi->instance().return_machine(mi);
				} else {
					mi->instance().workspace_free(mi);
				}
			#ifdef ENABLE_TIMING
				TIMING_LOCATION(t3);
				timing_destr.add(t2, t3);
			#endif
			};
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
	return (MachineInstance*) priv_task->priv;
}

void TenantInstance::set_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	printf("*** DynCall %s is registered as 0x%X\n", name.c_str(), hash);
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
		throw std::runtime_error("set_dynamic_call: Hash collision for " + name);
	}
	m_dynamic_functions.emplace(hash, std::move(handler));
}
void TenantInstance::reset_dynamic_call(const std::string& name, ghandler_t handler)
{
	const uint32_t hash = crc32(name.c_str(), name.size());
	m_dynamic_functions.erase(hash);
	if (handler != nullptr) {
		set_dynamic_call(name, std::move(handler));
	}
}
void TenantInstance::set_dynamic_calls(std::vector<std::pair<std::string, ghandler_t>> vec)
{
	for (const auto& pair : vec) {
		set_dynamic_call(pair.first, std::move(pair.second));
	}
}
void TenantInstance::dynamic_call(uint32_t hash, MachineInstance& machine) const
{
	auto it = m_dynamic_functions.find(hash);
	if (it != m_dynamic_functions.end()) {
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
    size_t size = 0;
    FILE* f = fopen(filename.c_str(), "rb");
    if (f == NULL) throw std::runtime_error("Could not open file: " + filename);

    fseek(f, 0, SEEK_END);
    size = ftell(f);
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

} // kvm
