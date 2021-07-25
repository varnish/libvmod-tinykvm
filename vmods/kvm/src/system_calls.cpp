#include "tenant_instance.hpp"
#include "varnish.hpp"
extern "C" long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir);

namespace kvm {

void MachineInstance::setup_syscall_interface()
{
	tinykvm::Machine::install_unhandled_syscall_handler(
	[] (tinykvm::Machine& machine, unsigned scall) {
		auto& inst = *machine.get_userdata<MachineInstance>();
		switch (scall) {
			case 0x10000:
				machine.stop();
				break;
			case 0x10001: {
				auto regs = machine.registers();
				auto* dir = inst.directors().get(regs.rdi);
				kvm_SetBackend(inst.ctx(), dir);
				} break;
			case 0x11111: {
				auto regs = machine.registers();
				inst.tenant().dynamic_call(regs.rdi, inst);
				} break;
			default:
				printf("Unhandled system call: %u\n", scall);
				auto regs = machine.registers();
				regs.rax = -ENOSYS;
				machine.set_registers(regs);
		}
	});
}

} // kvm
