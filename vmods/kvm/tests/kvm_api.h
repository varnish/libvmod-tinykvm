#include <stdint.h>

__attribute__((naked, noinline, noreturn))
void sys_return_result(const char *ctype, uint64_t tlen, const char *content, uint64_t clen)
{
	asm("movw $0xFFFF, %%ax; outl %%eax, $0;\n" : : : "eax", "memory");
	__builtin_unreachable();
}

void return_result(const char *ctype, const char *content)
{
	sys_return_result(ctype, strlen(ctype), content, strlen(content));
}

#define DYNAMIC_CALL(name, hash) \
	asm(".global " #name "\n" \
	#name ":\n" \
	"	mov $0x11111, %rax\n" \
	"	mov " #hash ", %rdi\n" \
	"	syscall\n" \
	"   ret\n"); \
	extern long name();
DYNAMIC_CALL(goto_dns, 0x746238D2)
