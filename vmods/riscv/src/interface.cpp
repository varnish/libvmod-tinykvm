#include <libriscv/machine.hpp>

#include <EASTL/fixed_vector.h>
#include <include/syscall_helpers.hpp>
#include <include/threads.hpp>
#include <linux.hpp>

// avoid endless loops, code that takes too long and excessive memory usage
static const uint64_t MAX_BINARY       = 16'000'000;
//static const uint64_t MAX_INSTRUCTIONS = 2'000'000;
static const uint32_t MAX_MEMORY       = 32 * 1024 * 1024;
static const uint32_t BENCH_SAMPLES    = 100;

static const std::vector<std::string> env = {
	"LC_CTYPE=C", "LC_ALL=C", "USER=groot"
};

static inline uint64_t micros_now();
static inline uint64_t monotonic_micros_now();
using set_header_t = void (*) (void*, const char*);

extern "C" const char*
execute(set_header_t set_header, void* header,
	const char* binary, size_t len, uint64_t instr_max)
{
	const std::vector<uint8_t> vbin(binary, binary + len);
	State<4> state;
	// go-time: create machine, execute code
	riscv::Machine<riscv::RISCV32> machine { vbin, MAX_MEMORY };

	prepare_linux<riscv::RISCV32>(machine, {"program"}, env);
	setup_linux_syscalls(state, machine);
	setup_multithreading(state, machine);

	// run the machine until potential break
	bool break_used = false;
	machine.install_syscall_handler(0,
	[&break_used] (auto& machine) -> long {
		break_used = true;
		machine.stop();
		return 0;
	});

	// execute until we are inside main()
	uint32_t main_address = 0x0;
	try {
		main_address = machine.address_of("main");
		if (main_address == 0x0) {
			set_header(header, "X-Exception: The address of main() was not found");
		}
	} catch (std::exception& e) {
		set_header(header, std::string(std::string("X-Exception: ") + e.what()).c_str());
	}
	if (main_address != 0x0)
	{
		const uint64_t st0 = micros_now();
		asm("" : : : "memory");
		// execute insruction by instruction until
		// we have entered main(), then break
		try {
			while (LIKELY(!machine.stopped())) {
				machine.cpu.simulate();
				if (UNLIKELY(machine.cpu.instruction_counter() >= instr_max))
					break;
				if (machine.cpu.registers().pc == main_address)
					break;
			}
		} catch (std::exception& e) {
			set_header(header, std::string(std::string("X-Exception: ") + e.what()).c_str());
		}
		asm("" : : : "memory");
		const uint64_t st1 = micros_now();
		asm("" : : : "memory");
		set_header(header, ("X-Startup-Time: " + std::to_string(st1 - st0)).c_str());
		const auto instructions = machine.cpu.instruction_counter();
		set_header(header, ("X-Startup-Instructions: " + std::to_string(instructions)).c_str());
		// cache for 10 seconds (it's only the output of a program)
		set_header(header, "Cache-Control: max-age=10");
	}
	if (machine.cpu.registers().pc == main_address)
	{
		// reset PC here for benchmarking
		machine.cpu.reset_instruction_counter();
		// take a snapshot of the machine
		std::vector<uint8_t> program_state;
		machine.serialize_to(program_state);
		eastl::fixed_vector<uint64_t, BENCH_SAMPLES> samples;
		// begin benchmarking 1 + N samples
		for (int i = 0; i < 1 + BENCH_SAMPLES; i++)
		{
			machine.deserialize_from(program_state);
			state.output.clear();
			const uint64_t t0 = micros_now();
			asm("" : : : "memory");

			try {
				machine.simulate(instr_max);
				if (machine.cpu.instruction_counter() == instr_max) {
					set_header(header, "X-Exception: Maximum instructions reached");
					break;
				}
			} catch (std::exception& e) {
				set_header(header, std::string(std::string("X-Exception: ") + e.what()).c_str());
				break;
			}

			asm("" : : : "memory");
			const uint64_t t1 = micros_now();
			asm("" : : : "memory");
			if (i != 0)
				samples.push_back(t1 - t0);
		}

		set_header(header, ("X-Exit-Code: " + std::to_string(state.exit_code)).c_str());
		if (!samples.empty()) {
			std::sort(samples.begin(), samples.end());
			const uint64_t lowest = samples[0];
			const uint64_t median = samples[samples.size() / 2];
			const uint64_t highest = samples[samples.size()-1];
			set_header(header, ("X-Runtime-Lowest: " + std::to_string(lowest)).c_str());
			set_header(header, ("X-Runtime-Median: " + std::to_string(median)).c_str());
			set_header(header, ("X-Runtime-Highest: " + std::to_string(highest)).c_str());
		}
		const auto instructions = std::to_string(machine.cpu.instruction_counter());
		set_header(header, ("X-Instruction-Count: " + instructions).c_str());
		set_header(header, ("X-Binary-Size: " + std::to_string(len)).c_str());
		const size_t active_mem = machine.memory.pages_active() * 4096;
		set_header(header, ("X-Memory-Usage: " + std::to_string(active_mem)).c_str());
		const size_t highest_mem = machine.memory.pages_highest_active() * 4096;
		set_header(header, ("X-Memory-Highest: " + std::to_string(highest_mem)).c_str());
		const size_t max_mem = machine.memory.pages_total() * 4096;
		set_header(header, ("X-Memory-Max: " + std::to_string(highest_mem)).c_str());
		return strdup(state.output.c_str());
	}
	else {
		set_header(header, "X-Exception: Could not enter main()");
		set_header(header, ("X-Instruction-Count: " + std::to_string(instr_max)).c_str());
	}
	return nullptr;
}

#include <sys/time.h>
inline uint64_t micros_now()
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec * 1000000ul + ts.tv_nsec / 1000ul;
}

inline uint64_t monotonic_micros_now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000ul + ts.tv_nsec / 1000ul;
}
