
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
    hash ^= 0xFFFFFFFF;

	const int idx = inst.regex().find(hash);
	if (idx >= 0) {
		regs.rax = idx;
        cpu.set_registers(regs);
        return;
    }

	/* Compile new regex pattern */
#ifdef VARNISH_PLUS
	const char* error = "";
	int         error_offset = 0;
	auto* re = VRE_compile(pattern.c_str(), 0, &error, &error_offset);
	/* TODO: log errors here */
	if (re == nullptr) {
		printf("Regex compile error: %s\n", error);
		throw std::runtime_error(
			"The regex pattern did not compile: " + pattern);
	}
#else /* JIT */
	int  error = 0;
	int  error_offset = 0;
	auto* re = VRE_compile(pattern.c_str(), 0, &error, &error_offset, true);
	/* TODO: log errors here */
	if (re == nullptr) {
		printf("Regex compile error: %d\n", error);
		throw std::runtime_error(
			"The regex pattern did not compile: " + pattern);
	}
#endif

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

    auto subject = cpu.machine().string_or_view(vaddr, size);
#ifdef VARNISH_PLUS
    /* VRE_exec(const vre_t *code, const char *subject, int length,
        int startoffset, int options, int *ovector, int ovecsize,
        const volatile struct vre_limits *lim) */
    regs.rax =
        VRE_exec(entry.item, subject.c_str(), subject.size(), 0,
            0, nullptr, 0, nullptr);
#else
	/* int VRE_match(const vre_t *code, const char *subject, size_t length,
	    int options, const volatile struct vre_limits *lim); */
	regs.rax =
		VRE_match(entry.item, subject.c_str(), subject.size(), 0, nullptr);
#endif
    cpu.set_registers(regs);
}

static void syscall_regex_subst(vCPU& cpu, MachineInstance& inst)
{
    auto& regs = cpu.registers();
    const uint32_t idx   = regs.rdi;
    const uint64_t vbuffer = regs.rsi;
    const uint32_t vsubst = regs.rdx;
    const uint64_t dstaddr = regs.rcx;
    const uint32_t dstsize = regs.r8;
    const uint32_t flags   = regs.r9;

    auto& entry = inst.regex().get(idx);
    const auto buffer = cpu.machine().copy_from_cstring(vbuffer);
    const auto subst  = cpu.machine().copy_from_cstring(vsubst);
    const bool all = (flags & 1) == 1;

    const char* result =
        VRT_regsub(inst.ctx(), all, buffer.c_str(), entry.item, subst.c_str());
    if (result != nullptr) {
        const uint32_t max = std::min(dstsize, (uint32_t)__builtin_strlen(result)+1);
        cpu.machine().copy_to_guest(dstaddr, result, max);
        regs.rax = max;
    } else {
        regs.rax = -1;
    }
    cpu.set_registers(regs);
}

} // kvm
