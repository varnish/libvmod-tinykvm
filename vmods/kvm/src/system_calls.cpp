#include "tenant_instance.hpp"
#include "varnish.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
extern "C" long kvm_SetBackend(VRT_CTX, VCL_BACKEND dir);
//#define VERBOSE_SYSCALLS

#ifdef VERBOSE_SYSCALLS
#define SYSPRINT(fmt, ...) printf(fmt, __VA_ARGS__);
#else
#define SYSPRINT(fmt, ...) /* */
#endif

namespace kvm {

void MachineInstance::setup_syscall_interface()
{
	using namespace tinykvm;
	Machine::install_unhandled_syscall_handler(
	[] (Machine& machine, unsigned scall) {
		auto& inst = *machine.get_userdata<MachineInstance>();
		switch (scall) {
			case 0x10000:
				machine.stop();
				break;
			case 0x10001: {
				auto regs = machine.registers();
				auto* dir = inst.directors().item(regs.rdi);
				kvm_SetBackend(inst.ctx(), dir);
				} break;
			default:
				printf("Unhandled system call: %u\n", scall);
				auto regs = machine.registers();
				regs.rax = -ENOSYS;
				machine.set_registers(regs);
		}
	});
	Machine::install_syscall_handler(
		0, [] (Machine& machine) { // READ
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();
			SYSPRINT("READ to fd=%lld, data=0x%llX, size=%llu\n",
				regs.rdi, regs.rsi, regs.rdx);

			int fd = inst.m_fd.translate(regs.rdi);
			char* buffer = (char *)std::malloc(regs.rdx);
			if (buffer != nullptr)
			{
				ssize_t res = read(fd, buffer, regs.rdx);
				if (res > 0) {
					machine.copy_to_guest(regs.rsi, buffer, res);
				}
				std::free(buffer);
				regs.rax = res;
			} else {
				regs.rax = -1;
			}
			machine.set_registers(regs);
		});
	Machine::install_syscall_handler(
		3, [] (Machine& machine) { // CLOSE
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();
			SYSPRINT("CLOSE to fd=%lld\n", regs.rdi);

			int idx = inst.m_fd.find(regs.rdi);
			if (idx >= 0) {
				auto& entry = inst.m_fd.get(idx);
				regs.rax = close(entry.item);
				//entry.free();
			} else {
				regs.rax = -1;
			}
			machine.set_registers(regs);
		});
	Machine::install_syscall_handler(
		4, [] (Machine& machine) { // STAT
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();

			const auto vpath = regs.rdi;

			char path[256];
			machine.copy_from_guest(path, vpath, sizeof(path));
			path[sizeof(path)-1] = 0;

			struct stat vstat;
			regs.rax = stat(path, &vstat);
			SYSPRINT("STAT to path=%s, data=0x%llX = %lld\n",
				path, regs.rsi, regs.rax);
			if (regs.rax == 0) {
				machine.copy_to_guest(regs.rsi, &vstat, sizeof(vstat));
			}
			machine.set_registers(regs);
		});
	Machine::install_syscall_handler(
		5, [] (Machine& machine) { // FSTAT
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();

			int fd = inst.m_fd.translate(regs.rdi);
			struct stat vstat;
			regs.rax = fstat(fd, &vstat);
			if (regs.rax == 0) {
				machine.copy_to_guest(regs.rsi, &vstat, sizeof(vstat));
			}
			SYSPRINT("FSTAT to vfd=%lld, fd=%d, data=0x%llX = %lld\n",
				regs.rdi, fd, regs.rsi, regs.rax);
			machine.set_registers(regs);
		});
	Machine::install_syscall_handler(
		217, [] (Machine& machine) { // GETDENTS64
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();

			int fd = inst.m_fd.translate(regs.rdi);

			char buffer[2048];
			regs.rax = syscall(SYS_getdents64, fd, buffer, sizeof(buffer));
			if (regs.rax > 0) {
				machine.copy_to_guest(regs.rsi, buffer, regs.rax);
			}
			SYSPRINT("GETDENTS64 to vfd=%lld, fd=%d, data=0x%llX = %lld\n",
				regs.rdi, fd, regs.rsi, regs.rax);
			machine.set_registers(regs);
		});
	Machine::install_syscall_handler(
		257, [] (Machine& machine) { // OPENAT
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();

			const auto vpath = regs.rsi;
			const int  flags = regs.rdx;

			char path[256];
			machine.copy_from_guest(path, vpath, sizeof(path));
			path[sizeof(path)-1] = 0;

			int fd = openat(AT_FDCWD, path, flags);
			SYSPRINT("OPENAT fd=%lld path=%s = %d\n",
				regs.rdi, path, fd);

			if (fd > 0) {
				inst.m_fd.manage(fd, 0x1000 + fd);
				regs.rax = 0x1000 + fd;
			} else {
				regs.rax = -1;
			}
			machine.set_registers(regs);
		});
	Machine::install_output_handler(
	[] (Machine& machine, unsigned port, unsigned data)
	{
		auto& inst = *machine.get_userdata<MachineInstance>();
		switch (port) {
			case 0x1: /* Dynamic calls */
				inst.tenant().dynamic_call(data, inst);
				break;
		}
	});
}

} // kvm
