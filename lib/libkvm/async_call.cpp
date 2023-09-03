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
int kvm_async_invocation(VRT_CTX, const struct kvm_chain_item *invoc)
{
	/* Always reserve a new VM, regardless of priv task. */
	auto* tenant = (kvm::TenantInstance *)invoc->tenant;
	const bool debug = false;

	VSLb(ctx->vsl, SLT_VCL_Log,
		"%s: Calling on_get() asynchronously", tenant->config.name.c_str());

	try
	{
		auto prog = tenant->ref(ctx, debug);
		if (UNLIKELY(prog == nullptr))
			return false;

		// Reserve a machine through blocking queue.
		// May throw if dequeue from the queue times out.
		auto resv = prog->reserve_vm(ctx, tenant, std::move(prog));
		// prog is nullptr after this ^

		/* During startup the task_future is used to wait for initialization.
		   We are safely re-using it here to asynchronously wait for request to finish.

		   We can safely pass invoc because it is workspace-allocated. */
		auto* slot = resv.slot;
		slot->task_future = slot->tp.enqueue(
		[slot, invoc] () -> long {

			auto& machine = *slot->mi;
			try {

				/* Deliberately set CTX inside task function (acting as serializer). */
				machine.set_ctx(nullptr);

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

			} catch (const std::exception& e) {
				/* XXX: Where does this go? */
				(void)e;
			}

			machine.program().vm_free_function(slot);
			return 0L;
		});
		return true;

	} catch (std::exception& e) {
		// It makes no sense to reserve a VM without a request w/VSL
		VSLb(ctx->vsl, SLT_Error,
			"VM '%s' exception: %s", tenant->config.name.c_str(), e.what());
		return false;
	}
}
