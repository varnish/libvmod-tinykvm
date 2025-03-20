#pragma once

#include <tinykvm/machine.hpp>

struct PausedVMState
{
	tinykvm::tinykvm_x86regs regs;
	tinykvm::tinykvm_x86fpuregs fpu;
	//tinykvm::kvm_sregs sregs; // ??
};
