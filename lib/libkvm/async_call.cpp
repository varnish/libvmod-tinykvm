/**
 * @file async_call.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief Glue between C and C++ for KVM async calls.
 * @version 0.1
 * @date 2022-09-03
 * 
**/
#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "settings.hpp"
#include "varnish.hpp"
#include <stdexcept>
extern "C" {
#include "vtim.h"
#include "kvm_backend.h"
}

extern "C"
void kvm_async_invocation(VRT_CTX, kvm::VMPoolItem* slot,
	const struct kvm_chain_item *invoc)
{
	auto& machine = *slot->mi;
	machine.set_ctx(nullptr);

	try {
		if (UNLIKELY(slot->task_future.valid())) {
			throw std::runtime_error("Program is already running asynchronously");
		}
		VSLb(ctx->vsl, SLT_VCL_Log,
			"%s: Calling on_get() asynchronously", machine.name().c_str());

		/* During startup the task_future is used to wait for initialization.
		   We are safely re-using it here to asynchronously wait for request to finish.

		   We can safely pass invoc because it is workspace-allocated. */
		slot->task_vsl = ctx->vsl;
		slot->task_future = slot->tp.enqueue(
		[&machine, invoc] () -> long {

			const auto timeout = machine.max_req_time();
			const auto& prog = machine.program();
			auto& vm = machine.machine();

			/* Call the backend GET function */
			auto on_get_addr = prog.entry_at(kvm::ProgramEntryIndex::BACKEND_GET);
			if (UNLIKELY(on_get_addr == 0x0))
				throw std::runtime_error("The GET callback has not been registered");

			/* Call into VM doing a full pagetable/cache flush. */
			vm.timed_vmcall(on_get_addr, timeout, invoc->inputs.url, invoc->inputs.argument);

			/* Make sure no SMP work is in-flight. */
			vm.smp_wait();

			return 0L;
		});
		return;

	} catch (const std::exception& e) {
		VSLb(ctx->vsl, SLT_Error,
			"%s: VM async call exception: %s", machine.name().c_str(), e.what());
	}
}
