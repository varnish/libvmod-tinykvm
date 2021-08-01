#include "program_instance.hpp"
#include <tinykvm/rsp_client.hpp>

namespace kvm {

void MachineInstance::open_debugger(uint16_t port)
{
	// Make sure we are the first to trigger a breakpoint
	instance().rsp_mtx.lock();
	if (instance().rsp_script == nullptr)
	{
		instance().rsp_script = this;
		assert(instance().rspclient == nullptr);
		instance().rsp_mtx.unlock();

		// Listener activated
		tinykvm::RSP server { machine(), port };
		printf(">>> Remote debugger waiting on a breakpoint...\n");
		instance().rspclient = server.accept();

		// Check if accept failed
		if (instance().rspclient == nullptr) {
			instance().rsp_script = nullptr;
			return;
		}
		printf(">>> Remote debugger connected\n");

	} else if (instance().rsp_script != this) {
		// Someone else already listening or debugging
		instance().rsp_mtx.unlock();
		return;
	} else if (instance().rspclient == nullptr) {
		// Already listening, but not connected
		instance().rsp_mtx.unlock();
		return;
	} else {
		// Already connected
		instance().rsp_mtx.unlock();
	}

	// Begin debugging (without locks)
	auto& client = instance().rspclient;
	client->set_machine(machine());
	client->interrupt();
	//this->run_debugger_loop();
}

}
