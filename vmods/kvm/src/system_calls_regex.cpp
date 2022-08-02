#include "utils/crc32.hpp"

namespace kvm {

static void syscall_regex_compile(vCPU& cpu, MachineInstance& inst)
{
    auto& regs = cpu.registers();
    const uint64_t vaddr = regs.rdi;
    const uint16_t rsize = regs.rsi;

    std::string pattern;
    /* Zero-copy CRC32-C of regex string */
    uint32_t hash = 0xFFFFFFFF;
    cpu.machine().foreach_memory(vaddr, rsize,
        [&] (const std::string_view data) {
            hash = crc32c_hw(hash, data.begin(), data.size());
            pattern.append(data);
        });

	const int idx = inst.regex().find(hash);
	if (idx >= 0) {
		regs.rax = idx;
        cpu.set_registers(regs);
        return;
    }

	/* Compile new regex pattern */
	const char* error = "";
	int         error_offset = 0;
	auto* re = VRE_compile(pattern.c_str(), 0, &error, &error_offset);
	/* TODO: log errors here */
	if (re == nullptr) {
		printf("Regex compile error: %s\n", error);
		throw std::runtime_error(
			"The regex pattern did not compile: " + pattern);
	}

    /* Return the regex handle */
    regs.rax = inst.regex().manage(re, hash);
    cpu.set_registers(regs);
}

static void syscall_regex_free(vCPU& cpu, MachineInstance& inst)
{
    const auto& regs = cpu.registers();
    inst.regex().free(regs.rdi);
}

static void syscall_regex_match(vCPU& cpu, MachineInstance& inst)
{
    auto& regs = cpu.registers();
    const uint32_t idx   = regs.rdi;
    const uint64_t vaddr = regs.rsi;
    const uint32_t size  = regs.rdx;

    auto& entry = inst.regex().get(idx);

    const auto subject = cpu.machine().sequential_view(vaddr, size);
    if (!subject.empty())
    {
        regs.rax =
            VRE_exec(entry.item, subject.begin(), subject.size(), 0,
                0, nullptr, 0, nullptr) >= 0;
    } else {
        /* TODO: Streaming-like approach for big buffers. */
        std::string subject;
        subject.reserve(size);
        cpu.machine().foreach_memory(vaddr, size,
            [&] (const std::string_view data) {
                subject.append(data);
            });

        /* VRE_exec(const vre_t *code, const char *subject, int length,
            int startoffset, int options, int *ovector, int ovecsize,
            const volatile struct vre_limits *lim) */
        regs.rax =
            VRE_exec(entry.item, subject.c_str(), subject.size(), 0,
                0, nullptr, 0, nullptr) >= 0;
    }
    cpu.set_registers(regs);
}

} // kvm
