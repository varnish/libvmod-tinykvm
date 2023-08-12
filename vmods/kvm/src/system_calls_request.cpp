#include <tinykvm/machine.hpp>

static void syscall_request(tinykvm::vCPU& cpu, kvm::MachineInstance& inst)
{
	using namespace tinykvm;
	auto& regs = cpu.registers();

}
