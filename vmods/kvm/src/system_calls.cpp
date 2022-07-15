#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
using namespace tinykvm;
//#define VERBOSE_SYSCALLS

#ifdef VERBOSE_SYSCALLS
#define SYSPRINT(fmt, ...) printf(fmt, __VA_ARGS__);
#else
#define SYSPRINT(fmt, ...) /* */
#endif

#include "system_calls_http.cpp"
#include "system_calls_api.cpp"

namespace kvm {

void MachineInstance::sanitize_path(char* buffer, size_t buflen)
{
	buffer[buflen-1] = 0;
	const size_t len = strnlen(buffer, buflen);
	for (const auto& gucci_path : tenant().config.group.allowed_paths)
	{
		if (gucci_path.size() <= len &&
			memcmp(buffer, gucci_path.c_str(), gucci_path.size()) == 0) {
			//printf("Path OK: %.*s against %s\n",
			//	(int)len, buffer, gucci_path.c_str());
			return;
		}
	}
	printf("Path failed: %.*s\n", (int)len, buffer);
	throw std::runtime_error("Disallowed path used");
}

void MachineInstance::sanitize_file(char* buffer, size_t buflen)
{
	const auto& state_file = TenantConfig::guest_state_file;
	// Test against state file including the terminating zero:
	if (memcmp(buffer, state_file.c_str(), state_file.size()+1) == 0) {
		return;
	}
	throw std::runtime_error("Writable file must be the provided state file");
}

static void syscall_unknown(vCPU& cpu, MachineInstance& inst, unsigned scall)
{
	printf("%s: Unhandled system call %u\n",
		inst.name().c_str(), scall);
	auto regs = cpu.registers();
	regs.rax = -ENOSYS;
	cpu.set_registers(regs);
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
			case 0x10005: // SET_CACHEABLE
				syscall_set_cacheable(cpu, inst);
				return;
			case 0x10010: // BACKEND_RESPONSE
				syscall_backend_response(cpu, inst);
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
			case 0x10100:
				syscall_set_backend(cpu, inst);
				return;
			case 0x10700: // SHARED_MEMORY_AREA
				syscall_shared_memory(cpu, inst);
				return;
			case 0x10701: // STORAGE_MEMORY_SHARED
				syscall_storage_mem_shared(cpu, inst);
				return;
			case 0x10702: // ALL_MEMORY_SHARED
				syscall_all_mem_shared(cpu, inst);
				return;
			case 0x10707: // STORAGE CALL BUFFER
				syscall_storage_callb(cpu, inst);
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
			default:
				syscall_unknown(cpu, inst, scall);
		}
	});
	Machine::install_syscall_handler(
		0, [] (vCPU& cpu) { // READ
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();
			SYSPRINT("READ to fd=%lld, data=0x%llX, size=%llu\n",
				regs.rdi, regs.rsi, regs.rdx);
			// TODO: Make proper tenant setting for file sizes
			if (regs.rdx > 4*1024*1024) {
				regs.rax = -1; /* Buffer too big */
			} else {
				int fd = inst.m_fd.translate(regs.rdi);
				auto buffer = std::unique_ptr<char[]> (new char[regs.rdx]);
				/* TODO: Use fragmented readv buffer */
				ssize_t res = read(fd, buffer.get(), regs.rdx);
				if (res > 0) {
					cpu.machine().copy_to_guest(regs.rsi, buffer.get(), res);
				}
				regs.rax = res;
			}
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		1, [] (vCPU& cpu) { // WRITE
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();
			const int    fd = regs.rdi;
			const size_t bytes = regs.rdx;
			SYSPRINT("WRITE to fd=%lld, data=0x%llX, size=%llu\n",
				regs.rdi, regs.rsi, regs.rdx);
			// TODO: Make proper tenant setting for file sizes
			if (fd == 1 || fd == 2) {
				if (bytes > 1024*64) {
					regs.rax = -1;
					cpu.set_registers(regs);
					return;
				}
			}
			else if (bytes > 1024*1024*4) {
				regs.rax = -1;
				cpu.set_registers(regs);
				return;
			}
			// TODO: Use gather-buffers and writev instead
			auto buffer = std::unique_ptr<char[]> (new char[bytes]);
			cpu.machine().copy_from_guest(buffer.get(), regs.rsi, bytes);

			if (fd != 1 && fd != 2) {
				/* Ignore writes outside of stdout and stderr */
				int fd = inst.m_fd.translate(regs.rdi);
				regs.rax = write(fd, buffer.get(), bytes);
			}
			else {
				cpu.machine().print(buffer.get(), bytes);
				regs.rax = bytes;
			}
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		3, [] (vCPU& cpu) { // CLOSE
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();
			SYSPRINT("CLOSE to fd=%lld\n", regs.rdi);

			int idx = inst.m_fd.find(regs.rdi);
			if (idx >= 0) {
				auto& entry = inst.m_fd.get(idx);
				regs.rax = close(entry.item);
				entry.free();
			} else {
				regs.rax = -1;
			}
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		4, [] (vCPU& cpu) { // STAT
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();
			const auto vpath = regs.rdi;

			char path[256];
			cpu.machine().copy_from_guest(path, vpath, sizeof(path));
			inst.sanitize_path(path, sizeof(path));

			struct stat vstat;
			regs.rax = stat(path, &vstat);
			SYSPRINT("STAT to path=%s, data=0x%llX = %lld\n",
				path, regs.rsi, regs.rax);
			if (regs.rax == 0) {
				cpu.machine().copy_to_guest(regs.rsi, &vstat, sizeof(vstat));
			}
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		5, [] (vCPU& cpu) { // FSTAT
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();

			int fd = inst.m_fd.translate(regs.rdi);
			struct stat vstat;
			regs.rax = fstat(fd, &vstat);
			if (regs.rax == 0) {
				cpu.machine().copy_to_guest(regs.rsi, &vstat, sizeof(vstat));
			}
			SYSPRINT("FSTAT to vfd=%lld, fd=%d, data=0x%llX = %lld\n",
				regs.rdi, fd, regs.rsi, regs.rax);
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		8, [] (vCPU& cpu) { // LSEEK
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();
			int fd = inst.m_fd.translate(regs.rdi);
			regs.rax = lseek(fd, regs.rsi, regs.rdx);
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		79, [](vCPU& cpu) { // GETCWD
			auto regs = cpu.registers();

			const char fakepath[] = "/";
			if (sizeof(fakepath) <= regs.rsi) {
				cpu.machine().copy_to_guest(regs.rdi, fakepath, sizeof(fakepath));
				regs.rax = regs.rdi;
			} else {
				regs.rax = 0;
			}
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		217, [](vCPU& cpu) { // GETDENTS64
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();

			int fd = inst.m_fd.translate(regs.rdi);

			char buffer[2048];
			regs.rax = syscall(SYS_getdents64, fd, buffer, sizeof(buffer));
			if (regs.rax > 0)
			{
				cpu.machine().copy_to_guest(regs.rsi, buffer, regs.rax);
			}
			SYSPRINT("GETDENTS64 to vfd=%lld, fd=%d, data=0x%llX = %lld\n",
				regs.rdi, fd, regs.rsi, regs.rax);
			cpu.set_registers(regs);
		});
	Machine::install_syscall_handler(
		257, [] (vCPU& cpu) { // OPENAT
			auto& inst = *cpu.machine().get_userdata<MachineInstance>();
			auto regs = cpu.registers();

			const auto vpath = regs.rsi;
			const int  flags = regs.rdx;

			char path[PATH_MAX];
			cpu.machine().copy_from_guest(path, vpath, sizeof(path));
			bool write_flags = (flags & (O_WRONLY | O_RDWR)) != 0x0;
			if (!write_flags)
			{
				try {
					inst.sanitize_path(path, sizeof(path));

					int fd = openat(AT_FDCWD, path, flags);
					SYSPRINT("OPENAT fd=%lld path=%s = %d\n",
						regs.rdi, path, fd);

					if (fd > 0) {
						inst.m_fd.manage(fd, 0x1000 + fd);
						regs.rax = 0x1000 + fd;
						cpu.set_registers(regs);
						return;
					} else {
						regs.rax = -1;
					}
				} catch (...) {
					regs.rax = -1;
				}
			}
			if (write_flags || regs.rax == (__u64)-1)
			{
				try {
					inst.sanitize_file(path, sizeof(path));

					const auto& state_file = inst.tenant().config.allowed_file;
					int fd = openat(AT_FDCWD, state_file.c_str(), flags, S_IWUSR | S_IRUSR);
					SYSPRINT("OPENAT where=%lld path=%s flags=%X = fd %d\n",
						regs.rdi, path, flags, fd);

					if (fd > 0) {
						inst.m_fd.manage(fd, 0x1000 + fd);
						regs.rax = 0x1000 + fd;
					} else {
						regs.rax = -1;
					}
				} catch (...) {
					regs.rax = -1;
				}
			}
			cpu.set_registers(regs);
		});
	Machine::install_output_handler(
	[] (vCPU& cpu, unsigned port, unsigned data)
	{
		auto& inst = *cpu.machine().get_userdata<MachineInstance>();
		switch (port) {
			case 0x1: /* Dynamic calls */
				inst.tenant().dynamic_call(data, cpu, inst);
				break;
		}
	});
}

} // kvm
