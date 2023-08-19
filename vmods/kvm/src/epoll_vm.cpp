/**
 * @file epoll_vm.cpp
 * @author Alf-André Walla (fwsgonzo@hotmail.com)
 * @brief
 * @version 0.1
 * @date 2023-08-19
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

extern "C"
int kvm_vm_begin_epoll(VRT_CTX, VCL_PRIV task,
    const char *program, const int fd)
{
    auto* tenant = kvm_tenant_find(task, program);
    if (UNLIKELY(tenant == nullptr)) {
        VRT_fail(ctx, "No such program '%s' for steal()", program);
        return false;
    }

    VSLb(ctx->vsl, SLT_VCL_Log, "Stealing fd=%d for program %s",
        fd, program);

    /* XXX: Implement me. */
    close(fd);
    return true;
}
