#include "program_instance.hpp"
#include <tinykvm/rsp_client.hpp>

namespace kvm {

void MachineInstance::open_debugger(uint16_t port, float timeout)
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
	client->set_vcpu(machine().cpu());

	try {
		// Debugger loop
		while (client->process_one());
		// If the machine is still running, continue unless response set.
		const int RESP = this->is_storage() ? 2 : 1;
		if (!machine().stopped() && !this->response_called(RESP)) {
			machine().run(timeout);
		}
	} catch (...) {
		program().rsp_script = nullptr;
		throw;
	}

	// Release client
	program().rsp_script = nullptr;
}

void MachineInstance::storage_debugger(float timeout)
{
	auto& client = program().rspclient;
	if (client.get() == nullptr) {
		/* Without debugger, we can just run like normal. */
		machine().run(timeout);
		return;
	}
	auto& old_machine = client->machine();
	client->set_vcpu(machine().cpu());

	try {
		// Tell GDB that machine is "running"
		machine().stop(false);
		// Interrupt to tell GDB that we are somewhere else now
		client->interrupt();
		// Debugger loop (only while machine is running)
		while (client->process_one() && !machine().stopped());
	} catch (const std::exception& e) {
		printf("Error when debugging storage: %s\n", e.what());
		client->set_vcpu(old_machine.cpu());
		throw;
	}

	/* Debugger could have disconnected, let's finish. */
	if (!machine().stopped() && !this->response_called(2)) {
		machine().run(timeout);
	}

	/* Restore old machine used by debugger. */
	client->set_vcpu(old_machine.cpu());
}

}
