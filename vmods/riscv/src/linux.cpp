#include <libriscv/machine.hpp>

#include <EASTL/fixed_vector.h>
#include <include/syscall_helpers.hpp>
#include <include/threads.hpp>
#include <linux.hpp>
extern "C" __attribute__((format(printf, 2, 3)))
const char* WS_Printf(void *ws, const char *fmt, ...);

// avoid endless loops and excessive memory usage
static const uint32_t MAX_MEMORY     = 32 * 1024 * 1024;

static const std::vector<std::string> env = {
	"LC_CTYPE=C", "LC_ALL=C", "USER=groot"
};
static std::vector<std::string> args = {
	"hello_world", "test!"
};

static inline uint64_t micros_now();
static inline uint64_t monotonic_micros_now();
using set_header_t = void (*) (void*, const char*);

extern "C" const char*
execute_riscv(void* workspace, set_header_t set_header, void* header,
	const uint8_t* binary, size_t len, uint64_t instr_max)
{
	if (len < 64) {
		set_header(header, "X-Exception: Not an ELF binary");
		return "";
	}

	const std::vector<uint8_t> vbin(binary, binary + len);
	State<4> state;
	// go-time: create machine, execute code
	riscv::Machine<riscv::RISCV32> machine { vbin, MAX_MEMORY };

	prepare_linux<riscv::RISCV32>(machine, args, env);
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
		set_header(header, WS_Printf(workspace, "X-Exception: %s", e.what()));
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
			set_header(header, WS_Printf(workspace, "X-Exception: %s", e.what()));
		}
		asm("" : : : "memory");
		const uint64_t st1 = micros_now();
		asm("" : : : "memory");
		set_header(header, WS_Printf(workspace, "X-Startup-Time: %ld", st1 - st0));
		set_header(header, WS_Printf(workspace, "X-Startup-Instructions: %lu",
			machine.cpu.instruction_counter()));
		// cache for 10 seconds (it's only the output of a program)
		set_header(header, "Cache-Control: max-age=10");
	}
	if (machine.cpu.registers().pc == main_address)
	{
		// reset PC here for benchmarking
		machine.cpu.reset_instruction_counter();
		// continue executing main()
		const uint64_t t0 = micros_now();
		asm("" : : : "memory");

		try {
			machine.simulate(instr_max);
			if (machine.cpu.instruction_counter() >= instr_max) {
				set_header(header, "X-Exception: Maximum instructions reached");
			}
		} catch (std::exception& e) {
			set_header(header, WS_Printf(workspace, "X-Exception: %s", e.what()));
		}

		asm("" : : : "memory");
		const uint64_t t1 = micros_now();
		asm("" : : : "memory");

		set_header(header, WS_Printf(workspace, "X-Exit-Code: %d", state.exit_code));
		set_header(header, WS_Printf(workspace, "X-Runtime: %lu", t1 - t0));
		set_header(header, WS_Printf(workspace, "X-Instruction-Count: %lu",
			machine.cpu.instruction_counter()));
		set_header(header, WS_Printf(workspace, "X-Binary-Size: %zu", len));
		const size_t active_mem = machine.memory.pages_active() * 4096;
		set_header(header, WS_Printf(workspace, "X-Memory-Usage: %zu", active_mem));
		return WS_Printf(workspace, "%s", state.output.c_str());
	}
	else {
		set_header(header, "X-Exception: Could not enter main()");
		set_header(header, WS_Printf(workspace, "X-Instruction-Count: %lu", instr_max));
	}
	return "";
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
