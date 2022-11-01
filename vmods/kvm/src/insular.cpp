#include "tenants.hpp"

#include "common_defs.hpp"
#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <string_view>
extern "C" int kvm_callv(VRT_CTX, kvm::VMPoolItem* slot, const int index, const char *farg);

namespace insular
{
	struct FetchTenantsStuff
	{
		VRT_CTX;
		VCL_PRIV task;
		VCL_STRING url;
	};

	static kvm::Tenants tenants;
	static std::unique_ptr<kvm::TenantConfig> program = nullptr;
	static std::unique_ptr<kvm::TenantInstance> inst = nullptr;

	int init(VRT_CTX, VCL_PRIV task, const char *url)
	{
		try {
			kvm::TenantGroup grp{"insular"};
			grp.max_concurrency = 1;
			grp.ephemeral = false;

			program.reset(new kvm::TenantConfig(
				"insular",
				"/tmp/insular_elf", "", std::move(grp), url));
			inst.reset(new kvm::TenantInstance{ctx, *program});
			return 0;
		} catch (const std::exception& e) {
			fprintf(stderr, "Could not start program: %s\n", e.what());
			return -1;
		}
	}

} // insular

extern "C"
int insular_initial_program(VRT_CTX, VCL_PRIV task, const char* url)
{
	return insular::init(ctx, task, url);
}

extern "C"
int insular_execute(VRT_CTX, int entry, const char *farg)
{
	if (UNLIKELY(insular::inst == nullptr))
		return -1;
	try
	{
		auto& ti = *insular::inst;
		auto* mi = ti.tlsreserve(ctx, false);
		if (UNLIKELY(mi == nullptr)) {
			fprintf(stderr, "Program reserve error\n");
			return -1;
		}
		auto addr = mi->program().entry_at(entry);
		mi->set_ctx(ctx);

		auto& vm = mi->machine();
		const auto timeout = mi->max_req_time();
		/* Call the guest function at addr */
		vm.timed_reentry(addr, timeout, farg);

		const auto& regs = vm.registers();
		return regs.rdi;

	} catch (const tinykvm::MachineTimeoutException& mte) {
		fprintf(stderr, "Insular VM timed out (%f seconds)\n",
			mte.seconds());
		VSLb(ctx->vsl, SLT_Error,
			"Insular VM timed out (%f seconds)",
			mte.seconds());
	} catch (const tinykvm::MachineException& e) {
		fprintf(stderr, "Insular VM exception: %s (data: 0x%lX)\n",
			e.what(), e.data());
		VSLb(ctx->vsl, SLT_Error,
			"Insular VM exception: %s (data: 0x%lX)",
			e.what(), e.data());
	} catch (const std::exception& e) {
		fprintf(stderr, "Insular VM exception: %s\n", e.what());
		VSLb(ctx->vsl, SLT_Error, "VM call exception: %s", e.what());
	}
	return -1;
}