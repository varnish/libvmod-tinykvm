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

void MachineInstance::sanitize_file(char* buffer, size_t buflen)
{
	buffer[buflen-1] = 0;
	const size_t len = strnlen(buffer, buflen);

	const auto& gucci_file = tenant().config.allowed_file;
	if (gucci_file.size() <= len &&
		memcmp(buffer, gucci_file.c_str(), gucci_file.size()) == 0) {
		SYSPRINT("File OK: %.*s against %s\n",
			(int)len, buffer, gucci_file.c_str());
		return;
	}

	printf("File failed: %.*s\n", (int)len, buffer);
	throw std::runtime_error("Disallowed file used");
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
			case 0x1070A: { // VMCOMMIT
				auto regs = machine.registers();
				try {
					// 1. Make a linearized copy of this machine
					auto new_machine = std::make_shared<MachineInstance>(inst);
					// 2. Perform the live update process on new program
					inst.instance().commit_instance_live(new_machine);
					VSLb(inst.ctx()->vsl, SLT_VCL_Log,
						"vmcommit: New %s program committed and ready",
						inst.name().c_str());
					regs.rax = 0;
				} catch (const std::exception& e) {
					fprintf(stderr, "VMCommit exception: %s\n", e.what());
					regs.rax = -1;
				}
				machine.set_registers(regs);
				} break;
			case 0x10710: { // MULTIPROCESS
				auto regs = machine.registers();
				try {
					/* It's too expensive to schedule multiple workloads. */
					if (UNLIKELY(machine.smp_active())) {
						throw std::runtime_error("Multiprocessing already active");
					}
					size_t num_cpus = std::min(regs.rdi, 8ull);
					const size_t stack_size = 512 * 1024ul;
					machine.timed_smpcall(num_cpus,
						machine.mmap_allocate(num_cpus * stack_size),
						stack_size,
						regs.rsi,
						2.0f,
						(uint64_t) regs.rdx /* arg */
						);
					regs.rax = 0;
				} catch (const std::exception& e) {
					fprintf(stderr, "Multiprocess exception: %s\n", e.what());
					regs.rax = -1;
				}
				machine.set_registers(regs);
				} break;
			case 0x10711: { // MULTIPROCESS_WAIT
				auto regs = machine.registers();
				try {
					machine.smp_wait();
					regs.rax = 0;
				} catch (const std::exception& e) {
					fprintf(stderr, "Multiprocess wait exception: %s\n", e.what());
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
		1, [] (Machine& machine) { // WRITE
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();
			const int    fd = regs.rdi;
			const size_t bytes = regs.rdx;
			char buffer[4096];
			if (bytes > 4096) {
				/* Ignore too big a write? */
				regs.rax = -1;
			} else if (fd != 1 && fd != 2) {
				/* Ignore writes outside of stdout and stderr */
				int fd = inst.m_fd.translate(regs.rdi);
				machine.copy_from_guest(buffer, regs.rsi, bytes);
				regs.rax = write(fd, buffer, bytes);
			}
			else {
				machine.copy_from_guest(buffer, regs.rsi, bytes);
				machine.print(buffer, bytes);
				regs.rax = bytes;
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
		8, [] (Machine& machine) { // LSEEK
			auto& inst = *machine.get_userdata<MachineInstance>();
			auto regs = machine.registers();
			int fd = inst.m_fd.translate(regs.rdi);
			regs.rax = lseek(fd, regs.rsi, regs.rdx);
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
						machine.set_registers(regs);
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

					int fd = openat(AT_FDCWD, path, flags, S_IWUSR | S_IRUSR);
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
