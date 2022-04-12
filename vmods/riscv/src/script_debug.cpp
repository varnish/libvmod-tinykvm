#include "script.hpp"
#include "sandbox_tenant.hpp"
#include <libriscv/rsp_server.hpp>
#include <mutex>

namespace rvs {

long Script::finish_debugger()
{
	// resume until stopped
	const uint64_t max = max_instructions();
	while (!machine().stopped() && machine().instruction_counter() < max) {
		machine().cpu.step_one();
		machine().increment_counter(1);
	}
	return machine().cpu.reg(10);
}

void Script::open_debugger(uint16_t port)
{
	// Make sure we are the first to trigger a breakpoint
	instance().rsp_mtx.lock();
	if (instance().rsp_script == nullptr)
	{
		instance().rsp_script = this;
		assert(instance().rspclient == nullptr);
		instance().rsp_mtx.unlock();

		// Listener activated
		riscv::RSP<Script::MARCH> server { machine(), port };
		printf(">>> Remote debugger waiting on a breakpoint...\n");
		instance().rspclient = server.accept();

		// Check if accept failed
		if (instance().rspclient == nullptr) {
			instance().rsp_script = nullptr;
			return;
		}

		printf(">>> Remote debugger connected\n");
		// We have to skip past the syscall instruction
		machine().cpu.jump(machine().cpu.pc() + 4);

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
	this->run_debugger_loop();
}
long Script::resume_debugger()
{
	// start the machine
	machine().stop();
	// storage has serialized access, so don't debug that
	if (is_storage()) {
		return finish_debugger();
	}

	instance().rsp_mtx.lock();
	if (instance().rsp_script == this) {
		instance().rsp_mtx.unlock();
		m_currently_debugging = true;
		this->run_debugger_loop();
		m_currently_debugging = false;
	} else {
		instance().rsp_mtx.unlock();
	}
	return finish_debugger();
}
void Script::stop_debugger()
{
	instance().rsp_mtx.lock();
	auto& rspclient = instance().rspclient;
	if (instance().rsp_script == this && rspclient != nullptr) {
		instance().rsp_mtx.unlock();
		try {
			// Let the remote debugger finish (?)
			if (!rspclient->is_closed()) {
				rspclient->interrupt();
				while (rspclient->process_one());
			}
			// Re-lock again to free and close
			rspclient = nullptr;
		} catch (...) {}
		rspclient = nullptr;
		// Make sure zeroing rsp_script is last
		asm("" ::: "memory");
		instance().rsp_script = nullptr;
		printf(">>> Remote debugger closed\n");
		return;
	}
	instance().rsp_mtx.unlock();
}
void Script::run_debugger_loop()
{
	auto& rspclient = instance().rspclient;
	// skip if we are not connected
	if (rspclient != nullptr)
	{
		try {
			//rspclient->set_verbose(true);
			rspclient->set_machine(machine());
			while (!machine().stopped()
				&& rspclient->process_one());
		} catch (...) {
			printf("Exception while using RSP...\n");
			throw;
		}
	}
}

} // rvs
