#include "program_instance.hpp"
#include <tinykvm/rsp_client.hpp>

namespace kvm {

void MachineInstance::open_debugger(uint16_t port)
{
	// Make sure we are the first to trigger a breakpoint
	program().rsp_mtx.lock();
	if (program().rsp_script == nullptr)
	{
		program().rsp_script = this;
		assert(program().rspclient == nullptr);
		program().rsp_mtx.unlock();

		// Listener activated
		tinykvm::RSP server { machine(), port };
		printf(">>> Remote debugger waiting on a breakpoint...\n");
		program().rspclient = server.accept();

		// Check if accept failed
		if (program().rspclient == nullptr) {
			program().rsp_script = nullptr;
			return;
		}
		printf(">>> Remote debugger connected\n");

	} else if (program().rsp_script != this) {
		// Someone else already listening or debugging
		program().rsp_mtx.unlock();
		return;
	} else if (program().rspclient == nullptr) {
		// Already listening, but not connected
		program().rsp_mtx.unlock();
		return;
	} else {
		// Already connected
		program().rsp_mtx.unlock();
	}

	// Begin debugging (without locks)
	auto& client = program().rspclient;
	client->set_machine(machine());
	client->interrupt();
	//this->run_debugger_loop();
}

}
