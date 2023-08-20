/**
 * @file epoll_vm.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2023-20-08
 *
 * to_string() produces a string result after passing
 * data through a chain of VMs.
 * The strings are currently zero-terminated.
 *
 */
#include "common_defs.hpp"
#include "program_instance.hpp"
#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <unistd.h>
extern "C" kvm::TenantInstance* kvm_tenant_find(VCL_PRIV task, const char* name);

namespace kvm {

static void new_connection(VRT_CTX, kvm::TenantInstance* tenant, const int fd)
{
	/* Atomically reference the main program. */
	auto prog = tenant->ref(ctx, false);

	if (UNLIKELY(prog == nullptr)) {
		close(fd);
		return;
	}

	auto& epoll = prog->epoll_system(std::move(prog));
	/* NOTE: prog is null after this ^ */

	if (epoll.manage(fd) == false) {
		close(fd);
	}
}

} // kvm

extern "C"
int kvm_vm_begin_epoll(VRT_CTX, VCL_PRIV task,
	const char *program, const int fd)
{
	auto* tenant = kvm_tenant_find(task, program);
	if (UNLIKELY(tenant == nullptr)) {
		VRT_fail(ctx,
			"No such program '%s' for steal()", program);
		return false;
	}

	VSLb(ctx->vsl, SLT_VCL_Log, "Stealing fd=%d for program %s",
		fd, program);

	kvm::new_connection(ctx, tenant, fd);
	return true;
}
