#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "utils/crc32.hpp"
#include "varnish.hpp"
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
using namespace tinykvm;
//#define VERBOSE_SYSCALLS

#ifdef VERBOSE_SYSCALLS
#define SYSPRINT(fmt, ...) printf(fmt, __VA_ARGS__);
#else
#define SYSPRINT(fmt, ...) /* */
#endif

#include "system_calls_http.cpp"
#include "system_calls_regex.cpp"
#include "system_calls_fetch.cpp"
#include "system_calls_api.cpp"

namespace kvm {
extern void syscall_sockets_write(tinykvm::vCPU& cpu, MachineInstance&);

uint32_t crc32_kvm(vCPU& cpu, uint64_t vaddr, size_t rsize)
{
    uint32_t hash = 0xFFFFFFFF;
    cpu.machine().foreach_memory(vaddr, rsize,
        [&] (const std::string_view data) {
            hash = crc32c_hw(hash, data.begin(), data.size());
        });
	return hash ^ 0xFFFFFFFF;
}

static void syscall_unknown(vCPU& cpu, MachineInstance& inst, unsigned scall)
{
	fprintf(stderr, "%s: Unhandled system call %u\n",
		inst.name().c_str(), scall);
	auto& regs = cpu.registers();
	regs.rax = -ENOSYS;
	cpu.set_registers(regs);
}

static void syscall_log(vCPU& cpu, MachineInstance&)
{
	auto& regs = cpu.registers();
	const uint64_t g_buf = regs.rdi;
	const uint16_t g_len = regs.rsi;
	/* Log to VSL if VRT ctx and VSL is accessible. */
	cpu.machine().foreach_memory(g_buf, g_len,
		[&cpu] (std::string_view buffer)
		{
			cpu.machine().print(buffer.begin(), buffer.size());
		});
}

void MachineInstance::setup_syscall_interface()
{
	Machine::install_unhandled_syscall_handler(
	[] (vCPU& cpu, unsigned scall) {
		auto& inst = *cpu.machine().get_userdata<MachineInstance>();
		switch (scall) {
			case 0x10000: // REGISTER_FUNC
				syscall_register_func(cpu, inst);
				return;
			case 0x10001: // WAIT_FOR_REQUESTS
				syscall_wait_for_requests(cpu, inst);
				return;
			case 0x10002: // PAUSE_FOR_REQUESTS
				syscall_pause_for_requests(cpu, inst);
				return;
			case 0x10005: // SET_CACHEABLE
				syscall_set_cacheable(cpu, inst);
				return;
			case 0x10010: // BACKEND_RESPONSE
				syscall_backend_response(cpu, inst);
				return;
			case 0x10012: // BACKEND_STREAMING_RESPONSE
				syscall_backend_streaming_response(cpu, inst);
				return;
			case 0x10011: // STORAGE_RETURN
				syscall_storage_return(cpu, inst);
				return;
			case 0x10013: // STORAGE_NORETURN
				syscall_storage_noreturn(cpu, inst);
				return;
			case 0x10020: // HTTP_APPEND
				syscall_http_append(cpu, inst);
				return;
			case 0x10021: // HTTP_SET
				syscall_http_set(cpu, inst);
				return;
			case 0x10022: // HTTP_FIND
				syscall_http_find(cpu, inst);
				return;
			case 0x10023: // HTTP_METHOD
				syscall_http_method(cpu, inst);
				return;
			case 0x10030: // REGEX_COMPILE
				syscall_regex_compile(cpu, inst);
				return;
			case 0x10031: // REGEX_FREE
				syscall_regex_free(cpu, inst);
				return;
			case 0x10032: // REGEX_MATCH
				syscall_regex_match(cpu, inst);
				return;
			case 0x10033: // REGEX_SUBST
				syscall_regex_subst(cpu, inst);
				return;
			case 0x10035: // REGEX_COPYTO
				syscall_regex_copyto(cpu, inst);
				return;
			case 0x10100:
				//syscall_set_backend(cpu, inst);
				return;
			case 0x10500: // SOCKETS_WRITEV
				syscall_sockets_write(cpu, inst);
				return;
			case 0x10700: // SHARED_MEMORY_AREA
				syscall_shared_memory(cpu, inst);
				return;
			case 0x10703: // MAKE_EPHEMERAL
				syscall_make_ephemeral(cpu, inst);
				return;
			case 0x10706: // IS_STORAGE
				syscall_is_storage(cpu, inst);
				return;
			case 0x10707: // STORAGE_ALLOW
				syscall_storage_allow(cpu, inst);
				return;
			case 0x10708: // STORAGE CALL VECTOR
				syscall_storage_callv(cpu, inst);
				return;
			case 0x10709: // ASYNC_STORAGE TASK
				syscall_storage_task(cpu, inst);
				return;
			case 0x1070A: // STOP_STORAGE TASK
				syscall_stop_storage_task(cpu, inst);
				return;
			case 0x10710: // MULTIPROCESS
				syscall_multiprocess(cpu, inst);
				return;
			case 0x10711: // MULTIPROCESS_ARRAY
				syscall_multiprocess_array(cpu, inst);
				return;
			case 0x10712: // MULTIPROCESS_CLONE
				syscall_multiprocess_clone(cpu, inst);
				return;
			case 0x10713: // MULTIPROCESS_WAIT
				syscall_multiprocess_wait(cpu, inst);
				return;
			case 0x10A00: // GET_MEMINFO
				syscall_memory_info(cpu, inst);
				return;
			case 0x20000: // CURL_FETCH
				syscall_fetch(cpu, inst);
				return;
			case 0x7F000: // LOG
				syscall_log(cpu, inst);
				return;
			case 0x7FDEB: // IS_DEBUG
				syscall_is_debug(cpu, inst);
				return;
			case 0x7F7F7: // BREAKPOINT
				syscall_breakpoint(cpu, inst);
				return;
			default:
				syscall_unknown(cpu, inst, scall);
		}
	});
} // setup_syscall_interface()

} // kvm
