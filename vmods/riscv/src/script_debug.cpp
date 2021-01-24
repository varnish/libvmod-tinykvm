#include "script.hpp"
#include "sandbox.hpp"
#include <libriscv/rsp_server.hpp>
#include <mutex>

long Script::finish_debugger()
{
	// resume until stopped
	const uint64_t max = max_instructions();
	while (!machine().stopped() && machine().instruction_counter() < max) {
		machine().cpu.simulate();
		machine().increment_counter(1);
	}
	return machine().cpu.reg(10);
}

long Script::resume_debugger()
{
	using namespace riscv;
	// make sure we can execute the remainder
	machine().stop(false);
	// storage has serialized access, so don't debug that
	if (is_storage()) {
		return finish_debugger();
	}

	const std::lock_guard<std::mutex> lock(instance().rsp_mtx);
	auto& rspclient = instance().rspclient;
	bool intr = (rspclient != nullptr);
	// create new RSP client if none exists
	if (rspclient == nullptr)
	{
		printf(">>> Remote debugger waiting...\n");
		RSP<Script::MARCH> server { machine(), 2159 };
		rspclient = server.accept();
		instance().rsp_script = this;
	}

	try {
		if (rspclient != nullptr)
		{
			//rspclient->set_verbose(true);
			rspclient->set_machine(machine());
			if (intr) rspclient->interrupt();
			while (!machine().stopped()
				&& rspclient->process_one());
		}
	} catch (...) {
		printf("Exception while using RSP...\n");
		throw;
	}
	return finish_debugger();
}
void Script::stop_debugger()
{
	const std::lock_guard<std::mutex> lock(instance().rsp_mtx);
	auto& rspclient = instance().rspclient;
	if (rspclient != nullptr && instance().rsp_script == this) {
		if (!rspclient->is_closed()) {
			rspclient->set_machine(machine());
			rspclient->interrupt();
			while (rspclient->process_one());
		}
		delete rspclient;
		rspclient = nullptr;
		instance().rsp_script = nullptr;
		printf(">>> Remote debugger closed\n");
	}
}
