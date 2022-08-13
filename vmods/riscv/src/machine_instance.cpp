#include "machine_instance.hpp"
#include <libriscv/rsp_server.hpp>

namespace rvs {

MachineInstance::MachineInstance(
	std::vector<uint8_t> elf,
	const vrt_ctx* ctx, SandboxTenant* vrm,
	bool debug)
	: binary{std::move(elf)},
	  script{binary, ctx, vrm, *this, false, debug},
	  storage{binary, ctx, vrm, *this, true, debug},
	  rspclient{nullptr},
	  callback_entries {}
{
	// sym_vector is now initialized, so we can run
	// through the main function of the tenants VM
	// for both the storage and main VM.
	storage.machine_initialize();
	script.machine_initialize();
}
MachineInstance::~MachineInstance()
{
}

} // rvs
