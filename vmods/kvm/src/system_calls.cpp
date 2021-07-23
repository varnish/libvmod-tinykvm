#include "machine_instance.hpp"

namespace kvm {

void MachineInstance::setup_syscall_interface()
{
	tinykvm::Machine::install_unhandled_syscall_handler(
	[] (tinykvm::Machine& machine, unsigned scall) {
		switch (scall) {
			case 0x10000:
				machine.stop();
				break;
			default:
				printf("Unhandled system call: %u\n", scall);
				auto regs = machine.registers();
				regs.rax = -ENOSYS;
				machine.set_registers(regs);
		}
	});
}

} // kvm
