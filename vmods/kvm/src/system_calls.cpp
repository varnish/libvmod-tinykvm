#include "tenant_instance.hpp"
#include "program_instance.hpp"
#include "varnish.hpp"
#include <cstring>
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
			case 0x10707: {
				auto regs = machine.registers();
				VirtBuffer buffers[1];
				buffers[0] = {
					.addr = (uint64_t)regs.rsi,  // src
					.len  = (uint64_t)regs.rdx   // len
				};
				regs.rax = inst.instance().storage_call(machine,
				/*  func      buf vector    dst     dstsize */
					regs.rdi, 1, buffers, regs.rcx, regs.r8);
				machine.set_registers(regs);
				} break;
			case 0x10708: {
				auto regs = machine.registers();
				const size_t n = regs.rsi;
				if (n <= 64) {
					VirtBuffer buffers[64];
					machine.copy_from_guest(buffers, regs.rdx, n * sizeof(VirtBuffer));
					regs.rax = inst.instance().storage_call(machine,
					/*  func      buf vector    dst     dstsize */
						regs.rdi, n, buffers, regs.rcx, regs.r8);
				} else {
					regs.rax = -1;
				}
				machine.set_registers(regs);
				} break;
			default:
				printf("%s: Unhandled system call %u\n",
					inst.name().c_str(), scall);
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
			if (regs.rdx > 4*1024*1024) {
				regs.rax = -1; /* Buffer too big */
			} else {
				int fd = inst.m_fd.translate(regs.rdi);
				auto buffer = std::unique_ptr<char[]> (new char[regs.rdx]);
				/* TODO: Use fragmented readv buffer */
				ssize_t res = read(fd, buffer.get(), regs.rdx);
				if (res > 0) {
					machine.copy_to_guest(regs.rsi, buffer.get(), res);
				}
				regs.rax = res;
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
				entry.free();
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
			inst.sanitize_path(path, sizeof(path));

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
			try {
				inst.sanitize_path(path, sizeof(path));

				int fd = openat(AT_FDCWD, path, flags);
				SYSPRINT("OPENAT fd=%lld path=%s = %d\n",
					regs.rdi, path, fd);

				if (fd > 0) {
					inst.m_fd.manage(fd, 0x1000 + fd);
					regs.rax = 0x1000 + fd;
				} else {
					regs.rax = -1;
				}
			} catch (...) {
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
